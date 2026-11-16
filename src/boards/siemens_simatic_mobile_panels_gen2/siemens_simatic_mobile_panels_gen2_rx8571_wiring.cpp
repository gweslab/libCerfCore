#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/epson_rx8571/epson_rx8571_sa_wiring.h"
#include "../../socs/imx6/imx6_i2c_bus.h"

#include <cstdint>
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kI2c3Base = 0x021A8000u;
constexpr uint8_t kRtcSlaveAddress = 0x32u;

class SiemensSimaticMobilePanelsGen2Rx8571Wiring final : public EpsonRx8571SaWiring {
public:
    using EpsonRx8571SaWiring::EpsonRx8571SaWiring;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }

    void Attach(I2cSlave* device) override { emu_.Get<Imx6I2cBus>().Register(device, kI2c3Base); }
    uint8_t SlaveAddress() const override { return kRtcSlaveAddress; }
};

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2Rx8571Wiring, EpsonRx8571SaWiring);

}
