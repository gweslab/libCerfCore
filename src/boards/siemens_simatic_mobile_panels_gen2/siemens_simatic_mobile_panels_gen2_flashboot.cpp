#include "siemens_simatic_mobile_panels_gen2_flashboot.h"

#include "../../boot/oms_object_tree.h"
#include "../../boot/zlib_inflate.h"
#include <cerfcore/core/crc32.h>
#include "siemens_simatic_mobile_panels_gen2_emmc_store.h"
#include "siemens_simatic_mobile_panels_gen2_factory_layout.h"

#include <cerfcore/core/byte_order.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2_flashboot {

namespace {

using cerf::fwf_oms::OmsObject;
using cerf::fwf_oms::OmsTree;

constexpr uint32_t kFirmwareClass = 18230u;
constexpr uint32_t kUpdateContainerClass = 18308u;
constexpr uint32_t kPolymorphContainerClass = 18311u;
constexpr uint32_t kTransportMorphClass = 18315u;
constexpr uint32_t kPersistenceMorphClass = 18316u;
constexpr uint32_t kExecutionMorphClass = 18317u;
constexpr uint32_t kImagePartClass = 18321u;
constexpr uint32_t kInPlaceStoreClass = 18327u;
constexpr uint32_t kLinearStoreClass = 18328u;
constexpr uint32_t kInPlaceBlobStreamedClass = 18616u;
constexpr uint32_t kPersistentStreamClass = 18791u;
constexpr uint32_t kMorphContainerLink = 18501u;
constexpr uint32_t kChecksumAlgorithm = 18508u;
constexpr uint32_t kCompressionAlgorithm = 18509u;
constexpr uint32_t kPartChecksum = 18510u;
constexpr uint32_t kPartSize = 18511u;
constexpr uint32_t kStoreSize = 18369u;
constexpr uint32_t kStorePath = 18364u;
constexpr uint32_t kStoreOffset = 18367u;
constexpr uint32_t kEntryPoint = 18555u;
constexpr uint32_t kInPlaceBlobData = 18619u;
constexpr uint32_t kPersistentStreamData = 18797u;
constexpr uint32_t kVersionClass = 18304u;
constexpr uint32_t kVersionText = 18306u;

constexpr uint32_t kStreamHeaderBytes = 8u;
constexpr uint32_t kMaxStreamBytes = 0x40000u;

const char kFlashboot[] = "/flashboot";
const char kRam[] = "/ram";
const char kCrc32[] = "crc32";
const char kDeflate[] = "deflate";

struct Part {
    std::string name;
    size_t object = OmsTree::kNone;
    size_t store = OmsTree::kNone;
};

bool Fail(std::string& error, const char* what, const std::string& detail = std::string()) {
    error = what;
    if (!detail.empty()) error += ": " + detail;
    return false;
}

size_t Linked(const OmsTree& tree, size_t morph) {
    uint32_t rid = 0;
    if (morph == OmsTree::kNone || !tree.At(morph).Link(kMorphContainerLink, rid)) return OmsTree::kNone;
    return tree.FindRid(rid);
}

size_t StoreOf(const OmsTree& tree, size_t part) {
    const size_t linear = tree.Child(part, kLinearStoreClass);
    return linear != OmsTree::kNone ? linear : tree.Child(part, kInPlaceStoreClass);
}

std::vector<Part> PartsOf(const OmsTree& tree, size_t container) {
    std::vector<Part> parts;
    for (size_t index : tree.Children(container, kImagePartClass))
        parts.push_back(Part{tree.At(index).Name(), index, StoreOf(tree, index)});
    return parts;
}

const Part* FindPart(const std::vector<Part>& parts, const std::string& name) {
    for (const auto& p : parts)
        if (p.name == name) return &p;
    return nullptr;
}

std::string Text(const OmsObject& obj, uint32_t id) {
    std::string value;
    obj.Text(id, value);
    return value;
}

bool LinearStore(const OmsTree& tree, const Part& part, const char* path, uint32_t& offset, uint32_t& size,
                 std::string& error) {
    if (part.store == OmsTree::kNone || tree.At(part.store).class_id != kLinearStoreClass)
        return Fail(error, "image part has no LinearStore", part.name);
    const OmsObject& store = tree.At(part.store);
    if (Text(store, kStorePath) != path) return Fail(error, "image part store path is not modelled", part.name);
    if (!store.Number(kStoreOffset, offset) || !store.Number(kStoreSize, size))
        return Fail(error, "image part store has no offset or size", part.name);
    return true;
}

bool ExpectedChecksum(const OmsObject& part, unsigned long& value) {
    char* end = nullptr;
    const std::string expected = Text(part, kPartChecksum);
    value = std::strtoul(expected.c_str(), &end, 16);
    return !expected.empty() && end && *end == '\0';
}

bool ChecksumMatches(const OmsObject& part, const std::vector<uint8_t>& bytes) {
    unsigned long value = 0;
    return ExpectedChecksum(part, value) && value == Crc32::Update(0u, bytes.data(), bytes.size());
}

bool InFlashboot(uint32_t offset, size_t size) {
    return offset <= siemens_simatic_mobile_panels_gen2_emmc::kFlashbootSize && size <= siemens_simatic_mobile_panels_gen2_emmc::kFlashbootSize - offset;
}

std::string VersionOf(const OmsTree& tree, size_t firmware) {
    const size_t version = tree.Child(firmware, kVersionClass);
    return version == OmsTree::kNone ? std::string() : Text(tree.At(version), kVersionText);
}

bool ReadStream(DiskImage& card, std::vector<uint8_t>& stream, uint32_t& stream_crc, std::string& error) {
    std::vector<uint8_t> header;
    if (!siemens_simatic_mobile_panels_gen2_emmc::ReadCardRange(card, siemens_simatic_mobile_panels_gen2_emmc::kFlashbootOff, kStreamHeaderBytes, header))
        return Fail(error, "/flashboot cannot be read");
    const uint32_t size = cerf::le::U32(header.data());
    if (size == 0u || size > kMaxStreamBytes) return Fail(error, "/flashboot holds no persistent stream");
    if (!siemens_simatic_mobile_panels_gen2_emmc::ReadCardRange(card, siemens_simatic_mobile_panels_gen2_emmc::kFlashbootOff + kStreamHeaderBytes, size, stream))
        return Fail(error, "/flashboot persistent stream cannot be read");
    stream_crc = cerf::le::U32(header.data() + 4u);
    if (Crc32::Update(0u, stream.data(), stream.size()) != stream_crc)
        return Fail(error, "/flashboot persistent stream has a CRC error");
    return true;
}

struct BootRecipe {
    OmsTree tree;
    size_t firmware = OmsTree::kNone;
    size_t hdd = OmsTree::kNone;
    size_t ram = OmsTree::kNone;
    uint32_t entry_va = 0;
    uint32_t stream_crc = 0;
};

bool LoadRecipe(DiskImage& card, BootRecipe& recipe, std::string& error) {
    std::vector<uint8_t> stream;
    if (!ReadStream(card, stream, recipe.stream_crc, error)) return false;
    OmsTree& tree = recipe.tree;
    if (!tree.Parse(stream.data(), stream.size(), error)) return false;
    recipe.firmware = tree.Root();
    if (tree.At(recipe.firmware).class_id != kFirmwareClass)
        return Fail(error, "/flashboot stream root is not a Firmware");
    const size_t morphs = tree.Child(recipe.firmware, kPolymorphContainerClass);
    recipe.hdd = Linked(tree, tree.Child(morphs, kPersistenceMorphClass));
    const size_t execute = tree.Child(morphs, kExecutionMorphClass);
    recipe.ram = Linked(tree, execute);
    if (recipe.hdd == OmsTree::kNone || recipe.ram == OmsTree::kNone ||
        !tree.At(execute).Number(kEntryPoint, recipe.entry_va))
        return Fail(error, "/flashboot stream has no persistence or execution morph");
    return true;
}

bool ReadStoredPart(DiskImage& card, const OmsTree& tree, const Part& part, std::vector<uint8_t>* packed,
                    std::string& error) {
    constexpr uint32_t kChunkBytes = 0x100000u;
    uint32_t offset = 0, size = 0;
    if (!LinearStore(tree, part, kFlashboot, offset, size, error)) return false;
    if (!InFlashboot(offset, size)) return Fail(error, "/flashboot image part lies past /flashboot", part.name);
    unsigned long expected = 0;
    if (!ExpectedChecksum(tree.At(part.object), expected))
        return Fail(error, "/flashboot image part has a CRC error", part.name);
    if (packed) {
        packed->clear();
        packed->reserve(size);
    }
    std::vector<uint8_t> chunk;
    uint32_t crc = 0u;
    for (uint32_t done = 0; done < size;) {
        const uint32_t n = std::min(kChunkBytes, size - done);
        if (!siemens_simatic_mobile_panels_gen2_emmc::ReadCardRange(
                card, siemens_simatic_mobile_panels_gen2_emmc::kFlashbootOff + offset + done, n, chunk))
            return Fail(error, "/flashboot image part cannot be read", part.name);
        crc = Crc32::Update(crc, chunk.data(), chunk.size());
        if (packed) packed->insert(packed->end(), chunk.begin(), chunk.end());
        done += n;
    }
    if (crc != expected) return Fail(error, "/flashboot image part has a CRC error", part.name);
    return true;
}

bool LoadImageRecipe(DiskImage& card, BootRecipe& recipe, std::string& error) {
    if (!LoadRecipe(card, recipe, error)) return false;
    const OmsObject& hdd = recipe.tree.At(recipe.hdd);
    if (Text(hdd, kChecksumAlgorithm) != kCrc32 || Text(hdd, kCompressionAlgorithm) != kDeflate)
        return Fail(error, "/flashboot image container algorithms are not modelled",
                    Text(hdd, kChecksumAlgorithm) + "/" + Text(hdd, kCompressionAlgorithm));
    return true;
}

}

bool HasBootRecipe(DiskImage& card, std::string& error) {
    BootRecipe recipe;
    return LoadRecipe(card, recipe, error);
}

bool VerifyBootImage(DiskImage& card, uint32_t& stream_crc, std::string& error) {
    BootRecipe recipe;
    if (!LoadImageRecipe(card, recipe, error)) return false;
    const std::vector<Part> stored = PartsOf(recipe.tree, recipe.hdd);
    if (stored.empty()) return Fail(error, "/flashboot image has no stored parts");
    for (const auto& part : stored)
        if (!ReadStoredPart(card, recipe.tree, part, nullptr, error)) return false;
    stream_crc = recipe.stream_crc;
    return true;
}

bool ReadBootImage(DiskImage& card, BootImage& out, std::string& error) {
    BootRecipe recipe;
    if (!LoadImageRecipe(card, recipe, error)) return false;
    const OmsTree& tree = recipe.tree;
    const size_t hdd = recipe.hdd;
    const size_t ram = recipe.ram;
    out.entry_va = recipe.entry_va;
    out.stream_crc = recipe.stream_crc;

    const std::vector<Part> stored = PartsOf(tree, hdd);
    const std::vector<Part> loaded = PartsOf(tree, ram);
    if (stored.empty() || stored.size() != loaded.size()) return Fail(error, "/flashboot image parts do not pair up");
    const bool single_part_image = stored.size() == 1u;
    uint32_t base = UINT32_MAX, end = 0;
    for (const auto& part : loaded) {
        uint32_t va = 0, size = 0;
        if (!LinearStore(tree, part, kRam, va, size, error)) return false;
        base = std::min(base, va);
        end = std::max(end, va + size);
    }
    out.base_va = base;
    if (!single_part_image) out.flat.assign(end - base, 0u);
    out.version = VersionOf(tree, recipe.firmware);
    size_t placed = 0;
    for (const auto& part : stored) {
        const Part* target = FindPart(loaded, part.name);
        uint32_t va = 0, raw = 0, ram_size = 0;
        if (!target) return Fail(error, "/flashboot image part has no RAM target", part.name);
        if (!LinearStore(tree, *target, kRam, va, ram_size, error)) return false;
        std::vector<uint8_t> packed;
        if (!ReadStoredPart(card, tree, part, &packed, error)) return false;
        if (!tree.At(part.object).Number(kPartSize, raw) || raw != ram_size)
            return Fail(error, "/flashboot image part size does not match its RAM target", part.name);
        std::vector<uint8_t> image = cerf::zlib_inflate::Decompress(packed.data(), packed.size(), raw);
        if (image.size() != raw) return Fail(error, "/flashboot image part does not inflate", part.name);
        if (single_part_image)
            out.flat = std::move(image);
        else
            std::copy(image.begin(), image.end(), out.flat.begin() + (va - base));
        placed += raw;
    }
    if (placed != out.flat.size()) return Fail(error, "/flashboot image parts leave a gap in RAM");
    return true;
}

bool ProgramFromFwf(DiskImage& card, const std::vector<uint8_t>& fwf, std::string& error) {
    OmsTree tree;
    if (!tree.Parse(fwf.data(), fwf.size(), error)) return false;
    const size_t firmware = tree.Child(tree.Root(), kFirmwareClass);
    if (firmware == OmsTree::kNone) return Fail(error, "FWF has no Firmware object");

    std::vector<size_t> owners = {firmware};
    for (size_t update : tree.Children(firmware, kUpdateContainerClass)) owners.push_back(update);
    for (size_t owner : owners) {
        const size_t morphs = tree.Child(owner, kPolymorphContainerClass);
        const size_t transport = Linked(tree, tree.Child(morphs, kTransportMorphClass));
        const size_t hdd = Linked(tree, tree.Child(morphs, kPersistenceMorphClass));
        if (transport == OmsTree::kNone || hdd == OmsTree::kNone)
            return Fail(error, "FWF container has no transport or persistence morph", tree.At(owner).Name());
        const bool checked = Text(tree.At(hdd), kChecksumAlgorithm) == kCrc32;
        const std::vector<Part> sources = PartsOf(tree, transport);
        for (const auto& part : PartsOf(tree, hdd)) {
            const Part* source = FindPart(sources, part.name);
            uint32_t offset = 0, size = 0;
            if (!LinearStore(tree, part, kFlashboot, offset, size, error)) return false;
            const size_t blob_object =
                source ? tree.Child(source->store, kInPlaceBlobStreamedClass) : OmsTree::kNone;
            const auto* blob = blob_object == OmsTree::kNone ? nullptr : tree.At(blob_object).Attribute(kInPlaceBlobData);
            if (!blob || blob->blob_size != size || !InFlashboot(offset, size))
                return Fail(error, "FWF image part has no transport data of its persistent size", part.name);
            const std::vector<uint8_t> bytes(fwf.begin() + blob->blob_off, fwf.begin() + blob->blob_off + size);
            if (checked && !ChecksumMatches(tree.At(part.object), bytes))
                return Fail(error, "FWF image part has a CRC error", part.name);
            if (!siemens_simatic_mobile_panels_gen2_emmc::WriteCardRange(card, siemens_simatic_mobile_panels_gen2_emmc::kFlashbootOff + offset, bytes.data(), size))
                return Fail(error, "/flashboot cannot be written", part.name);
        }
    }

    const size_t persistent = tree.Child(firmware, kPersistentStreamClass);
    const auto* stream = persistent == OmsTree::kNone ? nullptr : tree.At(persistent).Attribute(kPersistentStreamData);
    if (!stream || stream->blob_size == 0u || stream->blob_size > kMaxStreamBytes)
        return Fail(error, "FWF has no persistent stream");
    const Part store{tree.At(persistent).Name(), persistent, tree.Child(persistent, kLinearStoreClass)};
    uint32_t offset = 0, size = 0;
    if (!LinearStore(tree, store, kFlashboot, offset, size, error)) return false;
    std::vector<uint8_t> bytes(kStreamHeaderBytes);
    cerf::le::Put32(bytes.data(), static_cast<uint32_t>(stream->blob_size));
    cerf::le::Put32(bytes.data() + 4u, Crc32::Update(0u, fwf.data() + stream->blob_off, stream->blob_size));
    bytes.insert(bytes.end(), fwf.begin() + stream->blob_off, fwf.begin() + stream->blob_off + stream->blob_size);
    if (!InFlashboot(offset, bytes.size()) ||
        !siemens_simatic_mobile_panels_gen2_emmc::WriteCardRange(card, siemens_simatic_mobile_panels_gen2_emmc::kFlashbootOff + offset, bytes.data(), bytes.size()))
        return Fail(error, "/flashboot persistent stream cannot be written");
    return true;
}

bool ReadUpdateVolume(const std::vector<uint8_t>& fwf, std::vector<uint8_t>& volume, std::string& error) {
    OmsTree tree;
    if (!tree.Parse(fwf.data(), fwf.size(), error)) return false;
    const size_t firmware = tree.Child(tree.Root(), kFirmwareClass);
    if (firmware == OmsTree::kNone) return Fail(error, "FWF has no Firmware object");
    const std::vector<size_t> updates = tree.Children(firmware, kUpdateContainerClass);
    if (updates.size() != 1u) return Fail(error, "FWF does not hold exactly one UpdateContainer");
    const size_t morphs = tree.Child(updates.front(), kPolymorphContainerClass);
    const size_t transport = Linked(tree, tree.Child(morphs, kTransportMorphClass));
    if (transport == OmsTree::kNone) return Fail(error, "FWF UpdateContainer has no transport morph");
    const std::vector<Part> parts = PartsOf(tree, transport);
    if (parts.empty()) return Fail(error, "FWF UpdateContainer has no image parts");
    volume.clear();
    for (const auto& part : parts) {
        const size_t blob_object =
            part.store == OmsTree::kNone ? OmsTree::kNone : tree.Child(part.store, kInPlaceBlobStreamedClass);
        const auto* blob = blob_object == OmsTree::kNone ? nullptr : tree.At(blob_object).Attribute(kInPlaceBlobData);
        if (!blob) return Fail(error, "FWF UpdateContainer image part has no transport data", part.name);
        volume.insert(volume.end(), fwf.begin() + blob->blob_off, fwf.begin() + blob->blob_off + blob->blob_size);
    }
    return true;
}

}

}
