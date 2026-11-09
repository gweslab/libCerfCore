#include "../../peripherals/nec_vrc4171a/nec_vrc4171a_board.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "compaq_aero_1500_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kIsaIoBase        = 0x14000000u;
constexpr uint32_t kIsaMemBase       = 0x10000000u;
constexpr uint16_t kSlotBNone        = 0x0000u;
constexpr int      kIrqGiuPin        = 14;
constexpr uint8_t  kGpioInputsUndriven = 0x00u;

class CompaqAero1500Vrc4171aBoard : public NecVrc4171aBoard {
public:
    using NecVrc4171aBoard::NecVrc4171aBoard;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    uint32_t IsaIoBase() const override { return kIsaIoBase; }
    uint32_t IsaMemBase() const override { return kIsaMemBase; }
    uint16_t SlotBConfiguration() const override { return kSlotBNone; }
    std::string SlotALabel() const override { return "CompactFlash"; }
    bool     SensePullupsOnCardVcc(int) const override { return false; }
    uint8_t  GpioInputLevels() const override { return kGpioInputsUndriven; }
    bool     GpioPinsWired() const override { return true; }

    void SetIrqLevel(bool active) override {
        emu_.Get<Vr41xxGiu>().SetPinLevel(kIrqGiuPin, active);
    }
};

}

REGISTER_SERVICE_AS(CompaqAero1500Vrc4171aBoard, NecVrc4171aBoard);

}
