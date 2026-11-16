#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/ti_tsc2017/ti_tsc2017_wiring.h"
#include "../../socs/imx6/imx6_i2c_bus.h"

#include <cstdint>
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kI2c3Base = 0x021A8000u;
constexpr uint8_t kTouchSlaveAddress = 0x49u;

class SiemensSimaticMobilePanelsGen2Tsc2017Wiring final : public TiTsc2017Wiring {
public:
    using TiTsc2017Wiring::TiTsc2017Wiring;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }

    void Attach(I2cSlave* device) override { emu_.Get<Imx6I2cBus>().Register(device, kI2c3Base); }
    uint8_t SlaveAddress() const override { return kTouchSlaveAddress; }
};

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2Tsc2017Wiring, TiTsc2017Wiring);

}
