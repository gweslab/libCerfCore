#include "../unmapped_window_page_table_builder.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/memory_kind.h>
#include <cerfengine/unmapped_windows.h>
#include "../../core/board_config.h"
#include "../../socs/sh7709/sh7709_id.h"
#include "../../socs/sh77xx/sh77xx_area3_dram.h"
#include "../ram_param_id.h"
#include "hp_600_dram_bytes.h"
#include "hp_600_family_id.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kDramVaBase = cerfengine::UnmappedWindows::kCachedBase | kSh77xxRas3lPaBase;

class Hp600Sh7709PageTableBuilder : public UnmappedWindowPageTableBuilder {
public:
    using UnmappedWindowPageTableBuilder::UnmappedWindowPageTableBuilder;

    bool ShouldRegister() override {
        const BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    void OnReady() override {
        dram_bytes_ = emu_.Get<BoardConfig>().Enum(BoardConfigParamId::Ram, kHp600DramBytes);
        rom_        = PlaceEntryXipRom(kDramVaBase, dram_bytes_);
    }

    std::vector<DramRegion> CachedDramRegions() const override {
        return { { kDramVaBase, kSh77xxRas3lPaBase, dram_bytes_ } };
    }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        const bool repeats = (dram_bytes_ & (dram_bytes_ - 1u)) == 0u;
        return {
            { kDramVaBase, kSh77xxRas3lPaBase, dram_bytes_, MemoryKind::kRam,
              repeats ? kSh77xxRasWindowBytes : 0u },
            { rom_.va_base, rom_.pa_base, rom_.size, MemoryKind::kRom },
        };
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        return { { kDramVaBase, kSh77xxRas3lPaBase, kSh77xxRasWindowBytes }, rom_ };
    }

private:
    uint32_t   dram_bytes_ = 0;
    DramRegion rom_{};
};

}

REGISTER_SERVICE_AS(Hp600Sh7709PageTableBuilder, PageTableBuilder);

}
