#include "../../peripherals/nec_vrc4171a/nec_vrc4171a_lcd_board.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "compaq_aero_1500_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kFrameMemoryBase  = 0x0A000000u;
constexpr uint32_t kFrameMemoryBytes = 0x00200000u;
constexpr uint32_t kRegisterBase     = 0x0A800000u;
constexpr uint16_t kPanelSelect      = 0x0048u;
constexpr uint16_t kPanelSignals     = 0x0000u;
constexpr uint16_t kVerticalHalf     = 0x0078u;

class CompaqAero1500Vrc4171aLcdBoard : public NecVrc4171aLcdBoard {
public:
    using NecVrc4171aLcdBoard::NecVrc4171aLcdBoard;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    uint32_t FrameMemoryBase() const override { return kFrameMemoryBase; }
    uint32_t FrameMemoryBytes() const override { return kFrameMemoryBytes; }
    uint32_t RegisterBase() const override { return kRegisterBase; }
    NecVrc4171aPanelInterface PanelInterface() const override {
        return {kPanelSelect, kPanelSignals, kVerticalHalf};
    }
};

}

REGISTER_SERVICE_AS(CompaqAero1500Vrc4171aLcdBoard, NecVrc4171aLcdBoard);

}
