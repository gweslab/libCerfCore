#include "../unmapped_window_page_table_builder.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/memory_kind.h>
#include <cerfengine/unmapped_windows.h>
#include "../../core/board_config.h"
#include "../../socs/sh7729/sh7729_id.h"
#include "../../socs/sh77xx/sh77xx_area3_dram.h"
#include "../ram_param_id.h"
#include "hp_600_dram_bytes.h"
#include "hp_600_family_id.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kBank0Bytes = 16u << 20;

constexpr uint32_t CachedVa(uint32_t pa) { return cerfengine::UnmappedWindows::kCachedBase | pa; }

class Hp600Sh7729PageTableBuilder : public UnmappedWindowPageTableBuilder {
public:
    using UnmappedWindowPageTableBuilder::UnmappedWindowPageTableBuilder;

    bool ShouldRegister() override {
        const BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7729;
    }

    void OnReady() override {
        bank1_bytes_ = emu_.Get<BoardConfig>().Enum(BoardConfigParamId::Ram, kHp600DramBytes) - kBank0Bytes;
        rom_         = PlaceEntryXipRom(CachedVa(kSh77xxRas3lPaBase), kBank0Bytes);
    }

    std::vector<DramRegion> CachedDramRegions() const override {
        std::vector<DramRegion> r = { { CachedVa(kSh77xxRas3lPaBase), kSh77xxRas3lPaBase, kBank0Bytes } };
        if (bank1_bytes_ != 0u) r.push_back({ CachedVa(kSh77xxRas3uPaBase), kSh77xxRas3uPaBase, bank1_bytes_ });
        return r;
    }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        std::vector<BackedRegion> r = {
            { CachedVa(kSh77xxRas3lPaBase), kSh77xxRas3lPaBase, kBank0Bytes, MemoryKind::kRam,
              kSh77xxRasWindowBytes },
        };
        if (bank1_bytes_ != 0u) {
            r.push_back({ CachedVa(kSh77xxRas3uPaBase), kSh77xxRas3uPaBase, bank1_bytes_, MemoryKind::kRam,
                          kSh77xxRasWindowBytes });
        }
        r.push_back({ rom_.va_base, rom_.pa_base, rom_.size, MemoryKind::kRom });
        return r;
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        return {
            { CachedVa(kSh77xxRas3lPaBase), kSh77xxRas3lPaBase, kSh77xxRasWindowBytes },
            { CachedVa(kSh77xxRas3uPaBase), kSh77xxRas3uPaBase, kSh77xxRasWindowBytes },
            rom_,
        };
    }

private:
    uint32_t   bank1_bytes_ = 0;
    DramRegion rom_{};
};

}

REGISTER_SERVICE_AS(Hp600Sh7729PageTableBuilder, PageTableBuilder);

}
