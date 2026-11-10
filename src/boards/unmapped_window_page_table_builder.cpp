#include "unmapped_window_page_table_builder.h"

#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfengine/unmapped_windows.h>

#include <typeinfo>

namespace cerfcore {

uint32_t UnmappedWindowPageTableBuilder::VaToPa(uint32_t va) const {
    if (cerfengine::UnmappedWindows::Contains(va)) {
        return cerfengine::UnmappedWindows::Pa(va);
    }
    emu_.Get<Fatal>().Die("%s::VaToPa: VA 0x%08X is outside the unmapped windows",
                          typeid(*this).name(), va);
}

const ParsedRom& UnmappedWindowPageTableBuilder::PrimaryRom() const {
    auto& rom = emu_.Get<RomParserService>();
    if (!rom.Ok() || rom.Loaded().empty() || rom.Primary().xips.empty()) {
        emu_.Get<Fatal>().Die("%s: ROM not parsed", typeid(*this).name());
    }
    return rom.Primary();
}

DramRegion UnmappedWindowPageTableBuilder::CachedWindowRomSpan(uint32_t va_first,
                                                               uint32_t va_end) const {
    if (va_first < cerfengine::UnmappedWindows::kCachedBase || va_end <= va_first ||
        va_end > cerfengine::UnmappedWindows::kUncachedBase) {
        emu_.Get<Fatal>().Die("%s: ROM outside the cached unmapped window: va 0x%08X..0x%08X",
                              typeid(*this).name(), va_first, va_end);
    }
    return { va_first, cerfengine::UnmappedWindows::Pa(va_first), va_end - va_first };
}

void UnmappedWindowPageTableBuilder::RequireRamEndWithin(const ParsedROMHDR& hdr,
                                                         uint32_t dram_va,
                                                         uint32_t dram_size) const {
    if (hdr.ulRAMEnd <= dram_va || hdr.ulRAMEnd - dram_va > dram_size) {
        emu_.Get<Fatal>().Die("%s: ROMHDR ulRAMEnd 0x%08X does not fit the DRAM "
                              "at 0x%08X (0x%X bytes)", typeid(*this).name(),
                              hdr.ulRAMEnd, dram_va, dram_size);
    }
}

const ParsedROMHDR& UnmappedWindowPageTableBuilder::EntryXipHeader() const {
    const ParsedRom&    prim = PrimaryRom();
    const ParsedROMHDR* hdr  = prim.XipHeaderContaining(prim.entry_va);
    return hdr ? *hdr : prim.xips.front().toc.romhdr;
}

DramRegion UnmappedWindowPageTableBuilder::EntryXipRomRegion() const {
    const ParsedROMHDR& hdr = EntryXipHeader();
    return CachedWindowRomSpan(hdr.physfirst, hdr.physlast);
}

DramRegion UnmappedWindowPageTableBuilder::PlaceEntryXipRom(uint32_t dram_va,
                                                            uint32_t dram_size) const {
    const ParsedROMHDR& hdr = EntryXipHeader();
    const DramRegion    rom = EntryXipRomRegion();
    RequireRamEndWithin(hdr, dram_va, dram_size);

    LOG(Boot, "%s: ROM kva=0x%08X pa=0x%08X size=0x%X, DRAM kva=0x%08X size=0x%X "
              "(ulRAMEnd=0x%08X)\n", typeid(*this).name(), rom.va_base, rom.pa_base,
        rom.size, dram_va, dram_size, hdr.ulRAMEnd);
    return rom;
}

}
