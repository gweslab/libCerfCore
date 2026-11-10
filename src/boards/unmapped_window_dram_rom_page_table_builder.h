#pragma once

#include "unmapped_window_page_table_builder.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

struct UnmappedWindowDram {
    DramRegion region;
    uint32_t   decode_span;
    uint32_t   mapped_span;
};

class UnmappedWindowDramRomPageTableBuilder : public UnmappedWindowPageTableBuilder {
public:
    UnmappedWindowDramRomPageTableBuilder(CerfEmulator& emu, const UnmappedWindowDram& dram,
                                          const DramRegion& rom)
        : UnmappedWindowPageTableBuilder(emu), dram_(dram), rom_(rom),
          rom_from_entry_xip_(false) {}

    UnmappedWindowDramRomPageTableBuilder(CerfEmulator& emu, const UnmappedWindowDram& dram)
        : UnmappedWindowPageTableBuilder(emu), dram_(dram), rom_{},
          rom_from_entry_xip_(true) {}

    void OnReady() override;

    std::vector<DramRegion>   CachedDramRegions()   const final;
    std::vector<BackedRegion> BackedMemoryRegions() const final;
    std::vector<DramRegion>   MappedVaSpans()       const final;

private:
    const UnmappedWindowDram dram_;
    DramRegion               rom_;
    const bool               rom_from_entry_xip_;
};

}
