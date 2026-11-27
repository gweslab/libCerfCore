#include "imgfs_stub_injector.h"

#include "ce_imgfs_patcher.h"
#include "ce_imgfs_xpress.h"
#include "guest_additions_binaries.h"
#include "imgfs_victim_recomposer.h"
#include "rom_image_parse.h"
#include "rom_record_layout.h"
#include "../core/ascii_case.h"

#include <cerfcore/core/board_database.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

REGISTER_SERVICE(ImgfsStubInjector);

namespace {

namespace wk = cerf::ce_imgfs_walker;
namespace ip = cerf::ce_imgfs_patcher;
using cerf::le::U32;

constexpr uint32_t kCompressionXpr = 0x00525058u;

constexpr char kCantRun[] = "Guest Additions can't run on this ROM";

bool CarriesIndex(uint32_t magic) {
    return magic == wk::kMagicModule || magic == wk::kMagicSection ||
           magic == wk::kMagicModuleSection || magic == wk::kMagicFile;
}

bool TopLevel(uint32_t magic) {
    return magic == wk::kMagicModule || magic == wk::kMagicFile;
}

uint32_t IndexPtrOff(uint32_t magic) {
    return TopLevel(magic) ? wk::kModuleIndexPtrOff : wk::kSectionIndexPtrOff;
}

uint32_t IndexSizeOff(uint32_t magic) {
    return TopLevel(magic) ? wk::kModuleIndexSizeOff : wk::kSectionIndexSizeOff;
}

}

bool ImgfsStubInjector::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

ImgfsStubInjector::Layout ImgfsStubInjector::ReadLayout(ImgfsVolumeAccess& volume) {
    const std::vector<uint8_t>      h = volume.Read(0u, kImgfsSbSize);
    cerf::rom_image_parse::ImgfsSuperblock sb;
    if (!cerf::rom_image_parse::ReadImgfsSuperblock(h, 0, sb)) {
        LOG(GuestAdditions, "[ImgfsStub] the volume carries no IMGFS header that imgfs.dll mounts\n");
        emu_.Get<Fatal>().UserError(kCantRun);
    }
    const uint32_t sector = volume.SectorBytes();
    if (sb.dir_block_sectors == 0u || sb.dirent_size != kImgfsDirentSize ||
        sector % sb.chunks_per_sector != 0u || sb.data_block_bytes != wk::kImgfsPageSize) {
        LOG(GuestAdditions, "[ImgfsStub] IMGFS header: %u sectors per directory block, dirent "
                            "%u bytes, %u chunks per %u-byte sector, data block %u bytes\n",
            sb.dir_block_sectors, sb.dirent_size, sb.chunks_per_sector, sector,
            sb.data_block_bytes);
        emu_.Get<Fatal>().UserError(kCantRun);
    }
    Layout layout;
    layout.first_block = sb.first_dir_block;
    layout.block_bytes = sector * sb.dir_block_sectors;
    layout.chunk_bytes = sector / sb.chunks_per_sector;
    layout.compression = sb.compression;
    return layout;
}

std::set<uint32_t> ImgfsStubInjector::FreeChunks(const wk::VolumeRead&                 read,
                                                 const std::vector<wk::LoadedDirent>& dirents,
                                                 const wk::ImgfsModule&               victim,
                                                 const Layout&                        layout) {
    std::set<uint32_t> pool;
    const uint32_t     chunk = layout.chunk_bytes;
    auto chunks_of = [chunk](uint32_t offset, uint32_t length, auto&& visit) {
        if (length == 0u) return;
        const uint64_t end = uint64_t(offset) + length;
        for (uint64_t c = offset / chunk; c * chunk < end; ++c) visit(uint32_t(c));
    };
    auto add    = [&](uint32_t o, uint32_t n) { chunks_of(o, n, [&](uint32_t c) { pool.insert(c); }); };
    auto remove = [&](uint32_t o, uint32_t n) { chunks_of(o, n, [&](uint32_t c) { pool.erase(c); }); };
    auto index_extents = [&](uint32_t ptr, uint32_t size, auto&& visit) {
        if (ptr == 0u || size == 0u) return;
        visit(ptr, size);
        for (const wk::IndexRecord& rec : wk::ReadIndexRecords(read, ptr, size))
            if (rec.ptr != 0u) visit(rec.ptr, rec.comp_size);
    };

    index_extents(victim.mod_indexptr, victim.mod_indexsize, add);
    std::set<size_t> victim_dirents = {victim.dirent_off};
    for (const auto& s : victim.sections) {
        index_extents(s.sec_indexptr, s.sec_indexsize, add);
        victim_dirents.insert(s.dirent_off);
    }

    for (const wk::LoadedDirent& d : dirents) {
        remove(uint32_t(d.block), layout.block_bytes);
        const uint32_t magic = d.Magic();
        if (!CarriesIndex(magic)) continue;
        uint32_t name_ptr = 0, name_bytes = 0;
        if (wk::NameStringExtent(d.bytes.data() + wk::kDirentNameInfoOff, name_ptr, name_bytes))
            remove(name_ptr, name_bytes);
        if (victim_dirents.contains(d.off)) continue;
        index_extents(U32(d.bytes.data() + IndexPtrOff(magic)),
                      U32(d.bytes.data() + IndexSizeOff(magic)), remove);
    }
    return pool;
}

int ImgfsStubInjector::ReplaceVictims(ImgfsVolumeAccess& volume) {
    const Layout          layout = ReadLayout(volume);
    const wk::VolumeRead  read = [&volume](uint32_t offset, uint32_t length) {
        return volume.Read(offset, length);
    };
    const auto dirents = wk::EnumerateChain(read, layout.first_block, layout.block_bytes);
    const auto modules = wk::CollectModules(dirents, read);
    LOG(GuestAdditions, "[ImgfsStub] directory chain at 0x%X: %zu entries, %zu modules, "
                        "%u-byte blocks, %u-byte chunks\n",
        layout.first_block, dirents.size(), modules.size(), layout.block_bytes,
        layout.chunk_bytes);

    const std::string stub_path = emu_.Get<GuestAdditionsBinaries>().StubPath();
    int replaced = 0;
    for (const auto& victim : emu_.Get<BoardDatabase>().GaVideoDriverNames()) {
        for (const auto& m : modules) {
            if (!cerf::AsciiEqualIgnoreCase(m.name, victim)) continue;
            Replace(volume, read, m, FreeChunks(read, dirents, m, layout), layout, stub_path);
            ++replaced;
        }
    }
    return replaced;
}

void ImgfsStubInjector::Replace(ImgfsVolumeAccess& volume, const wk::VolumeRead& read,
                                const wk::ImgfsModule& victim, std::set<uint32_t> pool,
                                const Layout& layout, const std::string& stub_path) {
    const char* name = victim.name.c_str();
    const wk::Inflate inflate = [&](const std::vector<uint8_t>& packed, uint16_t full) {
        if (layout.compression != kCompressionXpr) {
            char method[5] = {};
            std::memcpy(method, &layout.compression, 4);
            emu_.Get<Fatal>().Die("[ImgfsStub] the module header of %s is compressed with the "
                                  "%s method, which the injection does not decode", name, method);
        }
        return cerf::ce_imgfs_xpress::Decompress(packed.data(), packed.size(), full);
    };
    const std::vector<uint8_t> orig_hdr =
        wk::ReadIndexData(read, inflate, victim.mod_indexptr, victim.mod_indexsize, victim.file_size);
    auto rc = emu_.Get<ImgfsVictimRecomposer>().Recompose(orig_hdr, victim.sections.size(), stub_path);
    if (!rc) emu_.Get<Fatal>().Die("[ImgfsStub] %s: the stub cannot be recomposed", name);

    const size_t pool_chunks = pool.size();
    auto alloc = [&](uint32_t bytes) -> uint32_t {
        const uint32_t need  = (bytes + layout.chunk_bytes - 1u) / layout.chunk_bytes;
        uint32_t       start = 0, run = 0, prev = 0;
        for (const uint32_t c : pool) {
            run  = (run != 0u && c == prev + 1u) ? run + 1u : 1u;
            start = (run == 1u) ? c : start;
            prev = c;
            if (run < need) continue;
            for (uint32_t i = 0; i < need; ++i) pool.erase(start + i);
            return start * layout.chunk_bytes;
        }
        emu_.Get<Fatal>().Die("[ImgfsStub] %s: the %zu chunks that the victim frees hold no "
                              "run of %u bytes", name, pool_chunks, bytes);
    };
    auto write = [&](const uint8_t* data, uint32_t length, uint32_t padded) {
        std::vector<uint8_t> bytes(padded, 0u);
        std::memcpy(bytes.data(), data, length);
        const uint32_t offset = alloc(padded);
        volume.Patch(offset, bytes);
        return offset;
    };
    auto put = [&](size_t dirent, uint32_t field, uint32_t value) {
        uint8_t b[4];
        cerf::le::Put32(b, value);
        volume.Patch(uint32_t(dirent + field), b);
    };

    const auto& slots = rc->slots;
    for (size_t i = 0; i < victim.sections.size(); ++i) {
        const size_t dirent = victim.sections[i].dirent_off;
        if (i >= slots.size()) {
            put(dirent, wk::kSectionIndexPtrOff, 0u);
            put(dirent, wk::kSectionIndexSizeOff, 0u);
            put(dirent, wk::kDirentFileSizeOff, 0u);
            continue;
        }
        const auto recs = ip::WritePagedData(
            slots[i].bytes, [&](uint32_t, const uint8_t* data, uint32_t length) {
                return write(data, length, wk::kImgfsPageSize);
            });
        const auto idx = ip::BuildIndexBlock(recs);
        put(dirent, wk::kSectionIndexPtrOff, write(idx.data(), uint32_t(idx.size()), uint32_t(idx.size())));
        put(dirent, wk::kSectionIndexSizeOff, uint32_t(idx.size()));
        put(dirent, wk::kDirentFileSizeOff, uint32_t(slots[i].bytes.size()));
    }

    const std::vector<uint8_t>& hdr = rc->new_hdr;
    const uint32_t hdr_off = write(hdr.data(), uint32_t(hdr.size()), uint32_t(hdr.size()));
    const auto     mod_idx = ip::BuildIndexBlock(uint32_t(hdr.size()), hdr_off);
    put(victim.dirent_off, wk::kModuleIndexPtrOff,
        write(mod_idx.data(), uint32_t(mod_idx.size()), uint32_t(mod_idx.size())));
    put(victim.dirent_off, wk::kModuleIndexSizeOff, uint32_t(mod_idx.size()));
    put(victim.dirent_off, wk::kDirentFileSizeOff, uint32_t(hdr.size()));

    LOG(GuestAdditions, "[ImgfsStub] '%s' replaced: dirent 0x%zX, %zu slot(s) in %zu victim "
                        "section(s), header 0x%X (%zu bytes), %zu of %zu free chunks used\n",
        name, victim.dirent_off, slots.size(), victim.sections.size(), hdr_off, hdr.size(),
        pool_chunks - pool.size(), pool_chunks);
}

}
