#include "../../peripherals/nec_vrc4172/vrc4172_gpio_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_cronos3e_family_id.h"

namespace cerfcore {

namespace {

class NecCronos3eVrc4172GpioConfig : public Vrc4172GpioConfig {
public:
    using Vrc4172GpioConfig::Vrc4172GpioConfig;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecCronos3e;
    }

    int      GiuIntPin() const override { return 10; }
    uint16_t InitialInputLevels() const override { return 0x3010u; }
};

}

REGISTER_SERVICE_AS(NecCronos3eVrc4172GpioConfig, Vrc4172GpioConfig);

}
