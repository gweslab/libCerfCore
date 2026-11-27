#include "rom_wmstore_parse.h"

#include "../storage/fat_volume_layout.h"
#include <cerfcore/core/byte_order.h>

#include <cstring>
#include <string>

namespace cerfcore {

namespace cerf::rom_image_parse {

namespace {

using cerf::le::U16;
using cerf::le::U32;
using cerf::le::U64;
using cerf::le::UN;

bool LocalHeaderAt(std::span<const uint8_t> raw, size_t at) {
    return raw.size() - at >= sizeof(kEscoZipLocalSignature) &&
           std::memcmp(raw.data() + at, kEscoZipLocalSignature,
                       sizeof(kEscoZipLocalSignature)) == 0;
}

bool EscoStoredMemberAt(std::span<const uint8_t> raw, size_t at, EscoMember& out) {
    if (raw.size() - at < kZipLocalHeaderSize || !LocalHeaderAt(raw, at)) return false;
    const uint8_t* h = raw.data() + at;
    if (U16(h, kZipMethodOff) != kZipMethodStore) return false;
    if ((U16(h, kZipFlagsOff) & kZipFlagDataDescriptor) != 0) return false;
    const uint32_t stored = U32(h, kZipCompressedSizeOff);
    if (stored != U32(h, kZipUncompressedSizeOff)) return false;
    const size_t name_len = U16(h, kZipNameLenOff);
    const uint64_t data = uint64_t(at) + kZipLocalHeaderSize + name_len
                        + U16(h, kZipExtraLenOff);
    if (data >= raw.size() || data + stored > raw.size()) return false;
    out.name.assign(reinterpret_cast<const char*>(h + kZipLocalHeaderSize), name_len);
    out.off   = size_t(data);
    out.bytes = stored;
    return true;
}

bool CertRange(std::span<const uint8_t> cert, uint32_t type, EscoRange& out) {
    if (cert.size() < kEscoCertBodyOff + kEscoCertRangeOff + kEscoCertRangeBytes)
        return false;
    const uint8_t* body = cert.data() + kEscoCertBodyOff;
    if (U32(body, kEscoCertRangeLenOff) != kEscoCertRangeBytes) return false;
    const uint8_t* rec = body + kEscoCertRangeOff;
    if (U32(rec) != type) return false;
    out.target = U32(rec, kEscoRangeTargetOff);
    out.drive  = U32(rec, kEscoRangeDriveOff);
    out.start = UN(rec + kEscoRangeStartOff, sizeof(uint64_t));
    out.size  = UN(rec + kEscoRangeSizeOff, sizeof(uint64_t));
    return out.size != 0u;
}

bool TerminatedWithin(const uint8_t* text, size_t chars) {
    for (size_t i = 0; i < chars; ++i) {
        if (U16(text, i * 2u) == 0) return true;
    }
    return false;
}

std::string PartitionName(const uint8_t* entry) {
    std::string name;
    for (size_t i = 0; i < kWmstorePartNameChars; ++i) {
        const uint16_t c = U16(entry, kWmstorePartNameOff + i * 2u);
        if (c == 0) break;
        if (c > 0x7Fu) return {};
        name.push_back(char(c));
    }
    return name;
}

}  /* namespace */

bool WmstoreStoreHeader(std::span<const uint8_t> sector, uint64_t disk_sectors,
                        uint32_t& superblock_lba) {
    if (sector.size() < kWmstoreSectorBytes || !fat_volume_layout::HasBootSignature(sector.data()))
        return false;
    const fat_volume_layout::MbrPartition first = fat_volume_layout::ReadMbrPartition(sector.data(), 0);
    const uint32_t lba = U32(sector.data(), kWmstoreSuperblockLbaOff);
    if (lba == 0 || lba >= disk_sectors || first.type != kWmstoreStoreType ||
        first.start_lba > kWmstoreStoreMaxStartLba)
        return false;
    superblock_lba = lba;
    return true;
}

bool WmstoreSuperblock(std::span<const uint8_t> sector, uint32_t& entry_count) {
    if (sector.size() < kWmstoreSectorBytes) return false;
    if (std::memcmp(sector.data(), kWmstoreSignature, sizeof(kWmstoreSignature)) != 0) return false;
    if (U32(sector.data(), kWmstoreAlignmentOff) == 0) return false;
    if (!TerminatedWithin(sector.data() + kWmstoreSuperblockNameOff, kWmstoreSuperblockNameChars))
        return false;
    entry_count = U32(sector.data(), kWmstoreEntryCountOff);
    return true;
}

bool WmstorePartitionEntry(std::span<const uint8_t> sector, WmstorePartition& out) {
    if (sector.size() < kWmstoreSectorBytes) return false;
    if (std::memcmp(sector.data(), kWmpartSignature, sizeof(kWmpartSignature)) != 0) return false;
    out.name      = PartitionName(sector.data());
    out.start_lba = U64(sector.data(), kWmstorePartStartLbaOff);
    out.size_lba  = U64(sector.data(), kWmstorePartSizeLbaOff);
    return true;
}

bool WmstoreParseXip(std::span<const uint8_t> partition, WmstoreXip& out) {
    EcecRecord ecec;
    if (!ReadEcec(partition, kRomSignatureOffset, ecec)) return false;

    const uint32_t base_va = ecec.ptoc - ecec.romhdr_off;
    ParsedXipRegion region;
    if (!ParseXipAtEcec(partition, ecec, base_va, region)) return false;
    const ParsedROMHDR& hdr = region.toc.romhdr;
    if (hdr.physfirst != base_va || hdr.physlast <= hdr.physfirst) return false;

    const uint32_t flat_size = hdr.physlast - hdr.physfirst;
    if (uint64_t(flat_size) > partition.size()) return false;

    out.base_va   = base_va;
    out.flat_size = flat_size;
    out.entry_va  = KernelEntryVa(partition, base_va, region.toc);
    return true;
}

bool EscoStoredMembers(std::span<const uint8_t> raw, std::vector<EscoMember>& out) {
    out.clear();
    for (size_t at = 0; LocalHeaderAt(raw, at);) {
        EscoMember m;
        if (!EscoStoredMemberAt(raw, at, m)) return false;
        at = m.off + m.bytes;
        out.push_back(std::move(m));
    }
    return !out.empty();
}

bool EscoImageRange(std::span<const uint8_t> raw, EscoRange& out) {
    std::vector<EscoMember> members;
    if (!EscoStoredMembers(raw, members)) return false;
    const std::string cert_name = members[0].name + kEscoCertSuffix;
    for (const auto& m : members) {
        if (m.name != cert_name) continue;
        if (!CertRange(raw.subspan(m.off, m.bytes), kEscoRangeImageWrite, out))
            return false;
        return out.size == members[0].bytes;
    }
    return false;
}

bool EscoEraseRange(std::span<const uint8_t> raw, EscoRange& out) {
    std::vector<EscoMember> members;
    if (!EscoStoredMembers(raw, members) || members.size() != 1u) return false;
    const EscoMember& m = members[0];
    if (!m.name.ends_with(kEscoCertSuffix)) return false;
    return CertRange(raw.subspan(m.off, m.bytes), kEscoRangeErase, out);
}

}  /* namespace cerf::rom_image_parse */

}
