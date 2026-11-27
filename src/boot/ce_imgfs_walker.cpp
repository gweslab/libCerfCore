#include "ce_imgfs_walker.h"

#include "ce_imgfs_xpress.h"
#include "rom_record_layout.h"

#include <cerfcore/core/byte_order.h>

#include <algorithm>
#include <cstring>
#include <set>

namespace cerfcore {

namespace cerf::ce_imgfs_walker {

namespace {

using cerf::le::U16;
using cerf::le::U32;

constexpr uint32_t kFtlValidFlagsMask = 0xFFF00000u;
constexpr uint32_t kFtlValidFlagsBits = 0xFFF00000u;
constexpr uint32_t kNoPhysPage        = 0xFFFFFFFFu;

bool FtlEntryLive(uint32_t ls, uint32_t fl) {
    return ls != kFtlErasedSector && (fl & kFtlValidFlagsMask) == kFtlValidFlagsBits;
}

std::string Utf16LeToAscii(const uint8_t* utf16, size_t bytes) {
    std::string out;
    out.reserve(bytes / 2);
    for (size_t i = 0; i + 1 < bytes; i += 2) {
        const uint16_t w = U16(utf16, i);
        if (w == 0) break;
        out.push_back((w < 128) ? char(w) : '?');
    }
    return out;
}

}

IndexRecord ReadIndexRecord(const uint8_t* rec) {
    return {U16(rec + kIndexRecOffCompSize), U16(rec + kIndexRecOffFullSize),
            U32(rec + kIndexRecOffPtr)};
}

Translator Translator::Detect(std::span<const uint8_t> raw,
                               size_t                   imgfs_base) {
    Translator t;
    t.imgfs_base_ = imgfs_base;
    t.raw_size_   = raw.size();
    if (imgfs_base >= raw.size()) {
        t.is_ftl_ = false;
        return t;
    }
    const size_t imgfs_size = raw.size() - imgfs_base;
    const size_t num_blocks = imgfs_size / kImgfsEraseBlock;

    size_t valid = 0;
    size_t total = 0;
    const size_t sample_blocks = std::min<size_t>(8, num_blocks);
    ForEachFtlMapEntry(raw, imgfs_base, sample_blocks,
                       [&](size_t, uint32_t, uint32_t ls, uint32_t fl) {
        ++total;
        if (FtlEntryLive(ls, fl)) ++valid;
    });
    if (total > 0 && valid * 10 >= total * 4) {
        t.is_ftl_ = true;
        uint32_t max_ls = 0;
        std::vector<std::pair<uint32_t, uint32_t>> raw_pairs;
        raw_pairs.reserve(num_blocks * kImgfsDataPagesPerBlock);
        ForEachFtlMapEntry(raw, imgfs_base, num_blocks,
                           [&](size_t blk, uint32_t e, uint32_t ls, uint32_t fl) {
            if (!FtlEntryLive(ls, fl)) return;
            raw_pairs.push_back({ls, FtlPhysPage(blk, e)});
            if (ls > max_ls) max_ls = ls;
        });
        t.sector_to_phys_.assign(size_t(max_ls) + 1, kNoPhysPage);
        for (auto [ls, phys] : raw_pairs) {
            t.sector_to_phys_[ls] = phys;
        }
        t.base_sector_ = 0;
        for (size_t i = 0; i < t.sector_to_phys_.size(); ++i) {
            if (t.sector_to_phys_[i] == 0) {
                t.base_sector_ = uint32_t(i);
                break;
            }
        }
    } else {
        t.is_ftl_ = false;
    }
    return t;
}

size_t Translator::Translate(uint32_t la) const {
    if (la == 0) return SIZE_MAX;
    if (!is_ftl_) {
        const size_t a = imgfs_base_ + la;
        return (a < raw_size_) ? a : SIZE_MAX;
    }
    const uint32_t page = la / kImgfsPageSize;
    const uint32_t sector = page + base_sector_;
    if (sector >= sector_to_phys_.size()) return SIZE_MAX;
    const uint32_t phys = sector_to_phys_[sector];
    if (phys == kNoPhysPage) return SIZE_MAX;
    const size_t a = imgfs_base_
                   + size_t(phys) * kImgfsPageSize
                   + (la & (kImgfsPageSize - 1));
    return (a < raw_size_) ? a : SIZE_MAX;
}

std::vector<uint8_t> Translator::Read(std::span<const uint8_t> raw,
                                       uint32_t                 la,
                                       uint32_t                 size) const {
    std::vector<uint8_t> out(size, 0);
    uint32_t remaining = size;
    uint32_t cursor = la;
    size_t   di = 0;
    while (remaining > 0) {
        const size_t abs_off = Translate(cursor);
        if (abs_off == SIZE_MAX) {
            break;
        }
        const uint32_t page_off = cursor & (kImgfsPageSize - 1);
        const uint32_t in_page = kImgfsPageSize - page_off;
        const uint32_t to_read = std::min(remaining, in_page);
        if (abs_off + to_read > raw.size()) break;
        std::memcpy(out.data() + di, raw.data() + abs_off, to_read);
        di       += to_read;
        cursor   += to_read;
        remaining -= to_read;
    }
    return out;
}

std::vector<IndexRecord> ReadIndexRecords(const VolumeRead& read, uint32_t indexptr,
                                          uint32_t indexsize) {
    std::vector<IndexRecord> records;
    if (indexptr == 0 || indexsize == 0) return records;
    const std::vector<uint8_t> idx = read(indexptr, indexsize);
    if (idx.size() < indexsize) return records;
    const uint32_t n_records = indexsize / kIndexRecSize;
    for (uint32_t i = 0; i < n_records; ++i) {
        const IndexRecord rec = ReadIndexRecord(idx.data() + i * kIndexRecSize);
        if (rec.IsTerminator()) break;
        records.push_back(rec);
    }
    return records;
}

std::vector<uint8_t> ReadIndexData(const VolumeRead& read,
                                    const Inflate&    inflate,
                                    uint32_t          indexptr,
                                    uint32_t          indexsize,
                                    uint32_t          expected_size) {
    std::vector<uint8_t> out;
    out.reserve(expected_size);
    for (const IndexRecord& rec : ReadIndexRecords(read, indexptr, indexsize)) {
        if (rec.ptr == 0) {
            out.insert(out.end(), rec.full_size, uint8_t(0));
            continue;
        }
        const std::vector<uint8_t> chunk = read(rec.ptr, rec.comp_size);
        if (rec.comp_size == rec.full_size) {
            out.insert(out.end(), chunk.begin(), chunk.end());
            continue;
        }
        const std::vector<uint8_t> dec = inflate(chunk, rec.full_size);
        out.insert(out.end(), dec.begin(), dec.end());
        if (dec.size() < rec.full_size) out.insert(out.end(), rec.full_size - dec.size(), uint8_t(0));
    }
    if (expected_size > 0 && out.size() > expected_size) {
        out.resize(expected_size);
    }
    return out;
}

std::vector<uint8_t> ReadIndexData(std::span<const uint8_t> raw,
                                    const Translator&        tr,
                                    uint32_t                 indexptr,
                                    uint32_t                 indexsize,
                                    uint32_t                 expected_size) {
    return ReadIndexData(
        [&](uint32_t offset, uint32_t length) { return tr.Read(raw, offset, length); },
        [](const std::vector<uint8_t>& packed, uint16_t full_size) {
            return ce_imgfs_xpress::Decompress(packed.data(), packed.size(), full_size);
        },
        indexptr, indexsize, expected_size);
}

std::vector<LoadedDirent> EnumerateChain(const VolumeRead& read,
                                         uint32_t          first_block,
                                         uint32_t          block_bytes) {
    std::vector<LoadedDirent> out;
    if (block_bytes <= kDirBlockHeadSize) return out;
    const uint32_t per_block = (block_bytes - kDirBlockHeadSize) / kImgfsDirentSize;
    std::set<uint32_t> visited;
    for (uint32_t at = first_block; at != 0 && visited.insert(at).second;) {
        const std::vector<uint8_t> block = read(at, block_bytes);
        if (block.size() < block_bytes || U32(block.data()) != kDirMagic) break;
        for (uint32_t i = 0; i < per_block; ++i) {
            const uint32_t rel = kDirBlockHeadSize + i * kImgfsDirentSize;
            LoadedDirent   d;
            d.off   = size_t(at) + rel;
            d.block = at;
            std::memcpy(d.bytes.data(), block.data() + rel, kImgfsDirentSize);
            out.push_back(d);
        }
        at = U32(block.data() + kDirBlockOffNext);
    }
    return out;
}

bool NameStringExtent(const uint8_t* ni, uint32_t& ptr, uint32_t& bytes) {
    const uint16_t length = U16(ni);
    if (length <= 4 || (U16(ni + 2) & 0x02)) return false;
    ptr   = U32(ni + 8);
    bytes = uint32_t(length) * 2;
    return true;
}

std::string ResolveName(const VolumeRead& read, const uint8_t* ni) {
    const uint16_t length = U16(ni);
    if (length == 0) return {};

    if (length <= 4) {
        return Utf16LeToAscii(ni + 4, size_t(length) * 2);
    }

    uint32_t ptr = 0, bytes = 0;
    if (!NameStringExtent(ni, ptr, bytes)) {
        const std::vector<uint8_t> dirent = read(U32(ni + 8), kImgfsDirentSize);
        if (dirent.size() == kImgfsDirentSize && U32(dirent.data()) == kMagicName) {
            return Utf16LeToAscii(dirent.data() + 4, kImgfsDirentSize - 4);
        }
        return {};
    }
    const std::vector<uint8_t> chars = read(ptr, bytes);
    if (chars.size() != bytes) return {};
    return Utf16LeToAscii(chars.data(), bytes);
}

namespace {

VolumeRead TranslatedRead(std::span<const uint8_t> raw, const Translator& tr) {
    return [raw, &tr](uint32_t offset, uint32_t length) { return tr.Read(raw, offset, length); };
}

}

std::string ResolveName(std::span<const uint8_t> raw,
                        const Translator&        tr,
                        const uint8_t*           ni) {
    return ResolveName(TranslatedRead(raw, tr), ni);
}

std::vector<ImgfsModule> CollectModules(const std::vector<LoadedDirent>& dirents,
                                         const VolumeRead&                read) {
    std::vector<ImgfsModule> mods;
    size_t i = 0;
    while (i < dirents.size()) {
        if (dirents[i].Magic() != kMagicModule) { ++i; continue; }
        const uint8_t* d = dirents[i].bytes.data();
        ImgfsModule m;
        m.dirent_off    = dirents[i].off;
        m.file_size     = U32(d + kDirentFileSizeOff);
        m.mod_indexptr  = U32(d + kModuleIndexPtrOff);
        m.mod_indexsize = U32(d + kModuleIndexSizeOff);
        m.name          = ResolveName(read, d + kDirentNameInfoOff);

        size_t j = i + 1;
        while (j < dirents.size()) {
            const uint32_t mj = dirents[j].Magic();
            if (mj == kMagicName) { ++j; continue; }
            if (mj != kMagicSection && mj != kMagicModuleSection) break;
            const uint8_t* sd = dirents[j].bytes.data();
            ImgfsModule::Section s;
            s.dirent_off    = dirents[j].off;
            s.file_size     = U32(sd + kDirentFileSizeOff);
            s.sec_indexptr  = U32(sd + kSectionIndexPtrOff);
            s.sec_indexsize = U32(sd + kSectionIndexSizeOff);
            s.name          = ResolveName(read, sd + kDirentNameInfoOff);
            m.sections.push_back(std::move(s));
            ++j;
        }
        mods.push_back(std::move(m));
        i = j;
    }
    return mods;
}

std::vector<ImgfsModule> CollectModules(std::span<const uint8_t> raw,
                                         const Translator&        tr,
                                         uint32_t                 first_dir_block,
                                         uint32_t                 dir_block_sectors) {
    const VolumeRead read = TranslatedRead(raw, tr);
    std::vector<LoadedDirent> dirents =
        EnumerateChain(read, first_dir_block, kImgfsPageSize * dir_block_sectors);
    for (LoadedDirent& d : dirents) d.off = tr.Translate(uint32_t(d.off));
    return CollectModules(dirents, read);
}

}

}
