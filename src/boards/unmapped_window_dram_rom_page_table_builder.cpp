#include "unmapped_window_dram_rom_page_table_builder.h"

namespace cerfcore {

void UnmappedWindowDramRomPageTableBuilder::OnReady() {
    if (rom_from_entry_xip_) {
        rom_ = PlaceEntryXipRom(dram_.region.va_base, dram_.region.size);
    }
}

std::vector<DramRegion> UnmappedWindowDramRomPageTableBuilder::CachedDramRegions() const {
    return { dram_.region };
}

std::vector<BackedRegion> UnmappedWindowDramRomPageTableBuilder::BackedMemoryRegions() const {
    return {
        { dram_.region.va_base, dram_.region.pa_base, dram_.region.size, MemoryKind::kRam,
          dram_.decode_span },
        { rom_.va_base, rom_.pa_base, rom_.size, MemoryKind::kRom },
    };
}

std::vector<DramRegion> UnmappedWindowDramRomPageTableBuilder::MappedVaSpans() const {
    return {
        { dram_.region.va_base, dram_.region.pa_base, dram_.mapped_span },
        rom_,
    };
}

}
