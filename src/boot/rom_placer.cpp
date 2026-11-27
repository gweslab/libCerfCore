#define NOMINMAX

#include "rom_placer.h"

#include "board_boot_placer.h"
#include "kernel_image_copier.h"
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/boot/rom_parser_service.h>

#include <cerfcore/boards/board_context.h>
#include "flat_container_placing_mode_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../boards/page_table_builder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace cerfcore {

REGISTER_SERVICE(RomPlacer);

bool RomPlacer::ShouldRegister() {
    return emu_.Get<BoardContext>().GetRomPlacingMode()
        == RomPlacingModeId::FlatContainer;
}

bool RomPlacer::PlaceRomXips(const ParsedRom& rom, bool volatile_only) {
    bool first_placed = false;
    for (size_t i = 0; i < rom.xips.size(); ++i) {
        const auto& xip = rom.xips[i];
        const uint32_t physfirst = xip.toc.romhdr.physfirst;
        const uint32_t physlast  = xip.toc.romhdr.physlast;
        if (physlast <= physfirst) continue;

        if (physfirst < xip.load_offset) {
            LOG(Caution,
                "RomPlacer %s: xip[%zu] physfirst=0x%08X below "
                "load_offset=0x%08X - skipping\n",
                rom.filename.c_str(), i, physfirst, xip.load_offset);
            continue;
        }

        const size_t file_off = size_t(physfirst - xip.load_offset);
        const size_t xip_len  = size_t(physlast - physfirst);
        if (file_off >= rom.flat.size()) {
            LOG(Caution,
                "RomPlacer %s: xip[%zu] file_off=0x%zX past flat "
                "size=%zu - skipping\n",
                rom.filename.c_str(), i, file_off, rom.flat.size());
            continue;
        }
        const size_t copy_len =
            std::min(xip_len, rom.flat.size() - file_off);

        emu_.Get<KernelImageCopier>().CopyToKernelVa(
            physfirst, rom.flat.data() + file_off, uint32_t(copy_len), volatile_only,
            rom.filename.c_str());
        if (i == 0) first_placed = true;
    }
    return first_placed;
}

void RomPlacer::PlaceB000FFSections(const ParsedRom& rom, bool volatile_only) {
    auto& page_tables = emu_.Get<PageTableBuilder>();
    auto& mem         = emu_.Get<EmulatedMemory>();
    const auto backed = page_tables.BackedMemoryRegions();

    /* Copy [va_lo, va_hi) of section `s` to its backed-region PA(s). A VA
       outside every backed band has no destination and is skipped; one
       straddling bands is split per band. */
    auto place = [&](uint32_t va_lo, uint32_t va_hi, const B000FFSection& s) {
        for (const auto& reg : backed) {
            const uint64_t lo = std::max<uint64_t>(va_lo, reg.va_base);
            const uint64_t hi = std::min<uint64_t>(va_hi,
                                    uint64_t(reg.va_base) + reg.size);
            if (lo >= hi) continue;
            const uint32_t pa = reg.pa_base + uint32_t(lo - reg.va_base);
            if (volatile_only && !emu_.Get<KernelImageCopier>().IsVolatilePa(pa)) continue;
            const size_t   src_off = s.data_off + size_t(lo - s.base);
            const uint32_t len     = uint32_t(hi - lo);
            if (src_off + len > rom.raw.size()) continue;
            mem.CopyIn(pa, rom.raw.data() + src_off, len);
            LOG(Boot,
                "RomPlacer %s: b000ff tail kva=0x%08X..0x%08X  pa=0x%08X  "
                "%u bytes%s\n",
                rom.filename.c_str(), uint32_t(lo), uint32_t(hi), pa, len,
                volatile_only ? " (replay)" : "");
        }
    };

    /* PlaceRomXips already copied every XIP region's physfirst..physlast, so
       here we add ONLY the section bytes outside all of them - the multi-XIP
       XIPCHAIN table living past the kernel ROMHDR's physlast. A single-XIP
       image has no such bytes, so its placement is unchanged. */
    for (const auto& s : rom.b000ff_sections) {
        if (s.size == 0) continue;
        uint64_t       cur = s.base;
        const uint64_t end = uint64_t(s.base) + s.size;
        while (cur < end) {
            uint64_t cover_end  = cur;   /* end of an XIP range covering cur */
            uint64_t next_start = end;   /* start of the next XIP range > cur */
            for (const auto& xip : rom.xips) {
                const uint64_t xs = xip.toc.romhdr.physfirst;
                const uint64_t xe = xip.toc.romhdr.physlast;
                if (xe <= xs) continue;
                if (xs <= cur && cur < xe)      cover_end  = std::max(cover_end, xe);
                else if (cur < xs && xs < next_start) next_start = xs;
            }
            if (cover_end > cur) { cur = cover_end; continue; }
            const uint64_t place_to = std::min(end, next_start);
            place(uint32_t(cur), uint32_t(place_to), s);
            cur = place_to;
        }
    }
}

void RomPlacer::OnReady() {
    auto& parser = emu_.Get<RomParserService>();
    if (!parser.Ok()) {
        LOG(Caution, "RomPlacer: parser not ready, nothing to place\n");
        return;
    }
    auto& page_tables = emu_.Get<PageTableBuilder>();
    if (page_tables.BackedMemoryRegions().empty()) {
        LOG(Boot, "RomPlacer: no backed regions; nothing to place\n");
        return;
    }
    auto& mem         = emu_.Get<EmulatedMemory>();

    for (const auto& r : page_tables.BackedMemoryRegions()) {
        if (r.kind == MemoryKind::kFlash) {
            std::memset(mem.Translate(r.pa_base), 0xFF, r.size);
            LOG(Boot, "RomPlacer: flash region pa=0x%08X size=0x%X "
                      "initialised to 0xFF (NOR erased state)\n",
                r.pa_base, r.size);
        }
    }

    for (const auto& rom : parser.Loaded()) {
        if (PlaceRomXips(rom, false) && &rom == &parser.Primary())
            placed_primary_ = &rom.xips[0];

        if (rom.is_b000ff) {
            /* Add the section bytes past every XIP's physfirst..physlast (the
               multi-XIP XIPCHAIN table); single-XIP images place nothing here. */
            PlaceB000FFSections(rom, /*volatile_only=*/false);
            continue;
        }

        {
            uint32_t image_va   = 0;
            uint32_t image_pa   = 0;
            uint64_t region_end = 0;
            bool     have_nv    = false;
            for (const auto& xip : rom.xips) {
                const uint32_t va = xip.load_offset;
                const uint32_t pa = page_tables.VaToPa(va);
                for (const auto& reg : page_tables.BackedMemoryRegions()) {
                    if (reg.kind != MemoryKind::kFlash &&
                        reg.kind != MemoryKind::kRom) continue;
                    if (pa < reg.pa_base) continue;
                    if (pa >= reg.pa_base + reg.size) continue;
                    image_va   = va;
                    image_pa   = pa;
                    region_end = uint64_t(reg.pa_base) + reg.size;
                    have_nv    = true;
                    break;
                }
                if (have_nv) break;
            }
            if (have_nv) {
                const size_t file_len =
                    std::min<size_t>(rom.flat.size(), size_t(region_end - image_pa));
                mem.CopyIn(image_pa, rom.flat.data(), file_len);
                LOG(Boot,
                    "RomPlacer %s: full non-volatile image %zu bytes placed at "
                    "kva=0x%08X..0x%08X  pa=0x%08X..0x%08X  "
                    "(fills inter-XIP gaps incl. XIPCHAIN table)\n",
                    rom.filename.c_str(), file_len,
                    image_va, uint32_t(image_va + file_len),
                    image_pa, uint32_t(image_pa + file_len));
            } else if (rom.has_imgfs) {
                LOG(Caution,
                    "RomPlacer %s: IMGFS present but no XIP region maps "
                    "to a Flash backed region - IMGFS bytes will not be "
                    "reachable, userspace mount will fail\n",
                    rom.filename.c_str());
            }
        }
    }

    if (auto* bp = emu_.TryGet<BoardBootPlacer>()) bp->PlaceAfterRom();

    emu_.Get<GuestColdBoot>().RegisterReplay([this] {
        for (const auto& rom : emu_.Get<RomParserService>().Loaded()) {
            PlaceRomXips(rom, /*volatile_only=*/true);
            if (rom.is_b000ff)
                PlaceB000FFSections(rom, /*volatile_only=*/true);
        }
    });
}

}
