#include "../../peripherals/nec_vrc4171a/nec_vrc4171a_board.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "ibm_workpad_z50_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kIsaIoBase        = 0x14000000u;
constexpr uint32_t kIsaMemBase       = 0x10000000u;
constexpr uint16_t kSlotBCompactFlash = 0x8000u;
constexpr int      kIrqGiuPin        = 9;
constexpr int      kCompactFlashBank = 1;

class IbmWorkpadZ50Vrc4171aBoard : public NecVrc4171aBoard {
public:
    using NecVrc4171aBoard::NecVrc4171aBoard;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
    }

    uint32_t IsaIoBase() const override { return kIsaIoBase; }
    uint32_t IsaMemBase() const override { return kIsaMemBase; }
    uint16_t SlotBConfiguration() const override { return kSlotBCompactFlash; }
    std::string SlotALabel() const override { return "PC Card"; }
    bool     SensePullupsOnCardVcc(int bank) const override { return bank == kCompactFlashBank; }

    void SetIrqLevel(bool active) override {
        emu_.Get<Vr41xxGiu>().SetPinLevel(kIrqGiuPin, active);
    }

    bool GpioPinsWired() const override { return false; }

    uint8_t GpioInputLevels() const override {
        emu_.Get<Fatal>().Die("IbmWorkpadZ50Vrc4171aBoard: read of a VRC4171A GPIO pin in input mode");
    }
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50Vrc4171aBoard, NecVrc4171aBoard);

}
