#include "../../peripherals/nec_vrc4172/vrc4172_gpio_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

namespace cerfcore {

namespace {

class NecMobilepro700Vrc4172GpioConfig : public Vrc4172GpioConfig {
public:
    using Vrc4172GpioConfig::Vrc4172GpioConfig;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    int      GiuIntPin() const override { return 1; }
    uint16_t InitialInputLevels() const override { return 0x0008u; }
};

}

REGISTER_SERVICE_AS(NecMobilepro700Vrc4172GpioConfig, Vrc4172GpioConfig);

}
