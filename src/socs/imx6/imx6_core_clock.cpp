#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx6_id.h"

namespace cerfcore {

namespace {

class Imx6CoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return SocId::IsImx6(emu_.Get<BoardContext>().GetSocId());
    }

    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{792000000u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Imx6CoreClock, ArmCoreClock);

}
