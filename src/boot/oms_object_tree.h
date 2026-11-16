#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

namespace cerf::fwf_oms {

struct OmsAttribute {
    uint32_t id = 0;
    uint8_t type = 0;
    uint32_t number = 0;
    std::string text;
    size_t blob_off = 0;
    size_t blob_size = 0;
};

struct OmsLink {
    uint32_t id = 0;
    uint32_t rid = 0;
};

struct OmsObject {
    uint32_t rid = 0;
    uint32_t class_id = 0;
    size_t parent = 0;
    std::vector<OmsAttribute> attributes;
    std::vector<OmsLink> links;
    std::vector<size_t> children;

    const OmsAttribute* Attribute(uint32_t id) const;
    std::string Name() const;
    bool Number(uint32_t id, uint32_t& out) const;
    bool Text(uint32_t id, std::string& out) const;
    bool Link(uint32_t id, uint32_t& target) const;
};

class OmsTree {
public:
    static constexpr size_t kNone = static_cast<size_t>(-1);

    bool Parse(const uint8_t* src, size_t size, std::string& error);

    const OmsObject& At(size_t index) const { return objects_[index]; }
    size_t Root() const { return objects_.empty() ? kNone : 0u; }
    size_t FindRid(uint32_t rid) const;
    size_t Child(size_t parent, uint32_t class_id) const;
    std::vector<size_t> Children(size_t parent, uint32_t class_id) const;

private:
    std::vector<OmsObject> objects_;
};

bool IsOmsStream(const uint8_t* src, size_t size);

}

}
