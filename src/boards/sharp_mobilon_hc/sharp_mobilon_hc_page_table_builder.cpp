#include "../unmapped_window_page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"
#include "sharp_mobilon_hc_memory.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kDramVaBase = 0x80000000u;

/* TX39 BIU System Address Map: DRAMBANK0CS1 0x00000000, DRAMBANK1CS1 0x02000000,
   DRAMBANK_LEN 0x02000000 (ENCS1DRAM clear). */
constexpr uint32_t kBank0PaBase     = 0x00000000u;
constexpr uint32_t kBank0DecodeSpan = 0x02000000u;
constexpr uint32_t kBank1VaBase     = 0x82000000u;
constexpr uint32_t kBank1PaBase     = 0x02000000u;
constexpr uint32_t kBank1DecodeSpan = 0x02000000u;

/* TX39 BIU CONFIG_REG 0x10C00000, len 0x00200000; kseg1 VA 0xB0C00000. */
constexpr uint32_t kRegsVaBase = 0xB0C00000u;
constexpr uint32_t kRegsPaBase = 0x10C00000u;
constexpr uint32_t kRegsSize   = 0x00200000u;

class SharpMobilonHcPageTableBuilder : public UnmappedWindowPageTableBuilder {
public:
    using UnmappedWindowPageTableBuilder::UnmappedWindowPageTableBuilder;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    void OnReady() override {
        const auto& memory = emu_.Get<SharpMobilonHcMemory>();
        bank0_size_ = memory.Bank0Bytes();
        bank1_size_ = memory.Bank1Bytes();
        rom_ = PlaceEntryXipRom(kDramVaBase, bank0_size_);
    }

    std::vector<DramRegion> CachedDramRegions() const override {
        std::vector<DramRegion> regions = { { kDramVaBase, kBank0PaBase, bank0_size_ } };
        if (bank1_size_ != 0u) regions.push_back({ kBank1VaBase, kBank1PaBase, bank1_size_ });
        return regions;
    }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        std::vector<BackedRegion> regions = {
            { kDramVaBase, kBank0PaBase, bank0_size_, MemoryKind::kRam, kBank0DecodeSpan },
        };
        if (bank1_size_ != 0u) {
            regions.push_back({ kBank1VaBase, kBank1PaBase, bank1_size_, MemoryKind::kRam,
                                kBank1DecodeSpan });
        }
        regions.push_back({ rom_.va_base, rom_.pa_base, rom_.size, MemoryKind::kRom });
        return regions;
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        std::vector<DramRegion> spans = { { kDramVaBase, kBank0PaBase, kBank0DecodeSpan } };
        if (bank1_size_ != 0u) spans.push_back({ kBank1VaBase, kBank1PaBase, kBank1DecodeSpan });
        spans.push_back(rom_);
        spans.push_back({ kRegsVaBase, kRegsPaBase, kRegsSize });
        return spans;
    }

private:
    uint32_t   bank0_size_ = 0;
    uint32_t   bank1_size_ = 0;
    DramRegion rom_{};
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHcPageTableBuilder, PageTableBuilder);

}
