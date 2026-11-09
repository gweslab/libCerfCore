#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../../socs/vr41xx/vr41xx_serial_wiring.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "compaq_aero_1500_family_id.h"

namespace cerfcore {

namespace {

constexpr int kRs232SupplyGpio = 48;

class CompaqAero1500SerialWiring : public Vr41xxGpioSuppliedSerialWiring {
public:
    using Vr41xxGpioSuppliedSerialWiring::Vr41xxGpioSuppliedSerialWiring;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    std::optional<Vr41xxSerialModem> ForSiu() const override {
        return Vr41xxSerialModem("COM1", Vr41xxGiu::kDcdPin);
    }

protected:
    int SupplyGpio() const override { return kRs232SupplyGpio; }
};

}

REGISTER_SERVICE_AS(CompaqAero1500SerialWiring, Vr41xxSerialWiring);

}
