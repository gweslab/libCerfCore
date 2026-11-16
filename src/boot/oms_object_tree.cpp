#include "oms_object_tree.h"

#include <cerfcore/core/byte_order.h>

#include <cstdio>

namespace cerfcore {

namespace cerf::fwf_oms {

namespace {

constexpr uint8_t kStreamStart = 0x03u;
constexpr uint8_t kObjectTag = 0xA1u;
constexpr uint8_t kEndTag = 0xA2u;
constexpr uint8_t kAttributeTag = 0xA3u;
constexpr uint8_t kLinkTag = 0xA4u;
constexpr uint32_t kNameAttrId = 233u;

constexpr uint8_t kTypeBool = 0x01u;
constexpr uint8_t kTypeUint = 0x04u;
constexpr uint8_t kTypeLong = 0x09u;
constexpr uint8_t kTypeBlob = 0x14u;
constexpr uint8_t kTypeString = 0x15u;
constexpr size_t kMaxVarintBytes = 5u;
constexpr size_t kMaxLongBytes = 10u;

class Cursor {
public:
    Cursor(const uint8_t* src, size_t size) : src_(src), size_(size) {}

    size_t pos() const { return pos_; }
    bool AtEnd() const { return pos_ >= size_; }

    bool Byte(uint8_t& out) {
        if (pos_ >= size_) return false;
        out = src_[pos_++];
        return true;
    }

    bool Varint(uint32_t& out) {
        out = 0;
        for (size_t i = 0; i < kMaxVarintBytes; ++i) {
            uint8_t b = 0;
            if (!Byte(b)) return false;
            out = (out << 7u) | (b & 0x7Fu);
            if ((b & 0x80u) == 0u) return true;
        }
        return false;
    }

    bool SkipLong() {
        for (size_t i = 0; i < kMaxLongBytes; ++i) {
            uint8_t b = 0;
            if (!Byte(b)) return false;
            if ((b & 0x80u) == 0u) return true;
        }
        return false;
    }

    bool Be32(uint32_t& out) {
        if (pos_ > size_ || size_ - pos_ < 4u) return false;
        out = cerf::be::U32(src_, pos_);
        pos_ += 4u;
        return true;
    }

    bool Skip(size_t n) {
        if (n > size_ - pos_) return false;
        pos_ += n;
        return true;
    }

    const uint8_t* At(size_t offset) const { return src_ + offset; }
    size_t size() const { return size_; }

private:
    const uint8_t* src_;
    size_t size_;
    size_t pos_ = 0;
};

std::string Where(const char* what, size_t at) {
    char text[96];
    std::snprintf(text, sizeof(text), "%s at offset 0x%zX", what, at);
    return text;
}

bool ReadAttribute(Cursor& c, OmsAttribute& attr, std::string& error) {
    const size_t at = c.pos();
    uint8_t flags = 0;
    if (!c.Varint(attr.id) || !c.Byte(flags) || !c.Byte(attr.type)) {
        error = Where("truncated attribute", at);
        return false;
    }
    if (flags != 0u) {
        error = Where("attribute value type with flags is not modelled", at);
        return false;
    }
    switch (attr.type) {
    case kTypeBool: {
        uint8_t b = 0;
        if (!c.Byte(b)) break;
        attr.number = b;
        return true;
    }
    case kTypeUint:
        if (!c.Varint(attr.number)) break;
        return true;
    case kTypeLong:
        if (!c.SkipLong()) break;
        return true;
    case kTypeString: {
        uint32_t length = 0;
        if (!c.Varint(length) || length > c.size() - c.pos()) break;
        attr.text.assign(reinterpret_cast<const char*>(c.At(c.pos())), length);
        return c.Skip(length);
    }
    case kTypeBlob: {
        uint32_t mode = 0;
        uint32_t length = 0;
        if (!c.Varint(mode)) break;
        if (mode > 1u) {
            error = Where("blob with a streaming mode other than 0 and 1", at);
            return false;
        }
        if (!c.Varint(length) || length > c.size() - c.pos()) break;
        attr.blob_off = c.pos();
        attr.blob_size = length;
        if (!c.Skip(length)) break;
        if (mode == 0u || length == 0u) return true;
        uint32_t next = 0;
        if (!c.Varint(next)) break;
        if (next != 0u) {
            error = Where("chunked blob with more than one chunk is not modelled", at);
            return false;
        }
        return true;
    }
    default:
        error = Where("attribute value type is not modelled", at);
        return false;
    }
    error = Where("truncated attribute value", at);
    return false;
}

}

const OmsAttribute* OmsObject::Attribute(uint32_t id) const {
    for (const auto& a : attributes)
        if (a.id == id) return &a;
    return nullptr;
}

std::string OmsObject::Name() const {
    std::string name;
    Text(kNameAttrId, name);
    return name;
}

bool OmsObject::Number(uint32_t id, uint32_t& out) const {
    const OmsAttribute* a = Attribute(id);
    if (!a || (a->type != kTypeUint && a->type != kTypeBool)) return false;
    out = a->number;
    return true;
}

bool OmsObject::Text(uint32_t id, std::string& out) const {
    const OmsAttribute* a = Attribute(id);
    if (!a || a->type != kTypeString) return false;
    out = a->text;
    return true;
}

bool OmsObject::Link(uint32_t id, uint32_t& target) const {
    for (const auto& l : links) {
        if (l.id != id) continue;
        target = l.rid;
        return true;
    }
    return false;
}

bool OmsTree::Parse(const uint8_t* src, size_t size, std::string& error) {
    objects_.clear();
    Cursor c(src, size);
    uint8_t tag = 0;
    if (!c.Byte(tag) || tag != kStreamStart) {
        error = "OMS stream does not start with 0x03";
        return false;
    }
    size_t current = kNone;
    while (!c.AtEnd()) {
        const size_t at = c.pos();
        c.Byte(tag);
        if (tag == kObjectTag) {
            OmsObject obj;
            uint32_t class_flags = 0, attr_id = 0, attr_value = 0;
            if (!c.Be32(obj.rid) || !c.Varint(obj.class_id) || !c.Varint(class_flags) || !c.Varint(attr_id) ||
                (attr_id != 0u && !c.Varint(attr_value))) {
                error = Where("truncated object header", at);
                return false;
            }
            if (current == kNone && !objects_.empty()) {
                error = Where("second root object", at);
                return false;
            }
            obj.parent = current;
            objects_.push_back(std::move(obj));
            const size_t index = objects_.size() - 1u;
            if (current != kNone) objects_[current].children.push_back(index);
            current = index;
            continue;
        }
        if (tag == kEndTag) {
            if (current == kNone) {
                error = Where("end of object outside any object", at);
                return false;
            }
            current = objects_[current].parent;
            if (current == kNone) return true;
            continue;
        }
        if (current == kNone) {
            error = Where("record outside any object", at);
            return false;
        }
        if (tag == kAttributeTag) {
            OmsAttribute attr;
            if (!ReadAttribute(c, attr, error)) return false;
            objects_[current].attributes.push_back(std::move(attr));
            continue;
        }
        if (tag == kLinkTag) {
            OmsLink link;
            if (!c.Varint(link.id) || !c.Be32(link.rid)) {
                error = Where("truncated link", at);
                return false;
            }
            objects_[current].links.push_back(link);
            continue;
        }
        error = Where("record tag is not modelled", at);
        return false;
    }
    error = "OMS stream ends inside an object";
    return false;
}

bool IsOmsStream(const uint8_t* src, size_t size) {
    return src && size >= 2u && src[0] == kStreamStart && src[1] == kObjectTag;
}

size_t OmsTree::FindRid(uint32_t rid) const {
    for (size_t i = 0; i < objects_.size(); ++i)
        if (objects_[i].rid == rid) return i;
    return kNone;
}

size_t OmsTree::Child(size_t parent, uint32_t class_id) const {
    if (parent == kNone) return kNone;
    for (size_t child : objects_[parent].children)
        if (objects_[child].class_id == class_id) return child;
    return kNone;
}

std::vector<size_t> OmsTree::Children(size_t parent, uint32_t class_id) const {
    std::vector<size_t> out;
    if (parent == kNone) return out;
    for (size_t child : objects_[parent].children)
        if (objects_[child].class_id == class_id) out.push_back(child);
    return out;
}

}

}
