#include "../unmapped_window_dram_rom_page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "compaq_aero_1500_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kDramVaBase   = 0x80000000u;
constexpr uint32_t kDramPaBase   = 0x00000000u;
constexpr uint32_t kDramSize     = 0x01000000u;
constexpr uint32_t kDramSpanSize = 0x04000000u;

constexpr uint32_t kRomVaBase = 0x9F000000u;
constexpr uint32_t kRomPaBase = 0x1F000000u;
constexpr uint32_t kRomSize   = 0x01000000u;

class CompaqAero1500PageTableBuilder : public UnmappedWindowDramRomPageTableBuilder {
public:
    explicit CompaqAero1500PageTableBuilder(CerfEmulator& emu)
        : UnmappedWindowDramRomPageTableBuilder(emu, {
              { kDramVaBase, kDramPaBase, kDramSize }, 0u, kDramSpanSize,
          }, { kRomVaBase, kRomPaBase, kRomSize }) {}

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }
};

}

REGISTER_SERVICE_AS(CompaqAero1500PageTableBuilder, PageTableBuilder);

}
