#include "../../cpu/sh/sh_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../sh7709/sh7709_id.h"
#include "../sh7729/sh7729_id.h"
#include "sh77xx_cpg.h"

namespace cerfcore {

namespace {

class Sh77xxCoreClock : public ShCoreClock {
public:
    using ShCoreClock::ShCoreClock;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Sh7709 || soc == SocId::Sh7729;
    }

    GuestCycleClock::Rate ResetRate() const override { return emu_.Get<Sh77xxCpg>().ResetCpuRate(); }
};

}

REGISTER_SERVICE_AS(Sh77xxCoreClock, ShCoreClock);

}
