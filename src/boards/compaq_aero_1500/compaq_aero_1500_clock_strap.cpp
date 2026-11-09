#include "../../socs/vr41xx/vr41xx_clock_strap.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "compaq_aero_1500_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kClksel69MHz = 0x3u;

class CompaqAero1500ClockStrap : public Vr41xxClockStrap {
public:
    using Vr41xxClockStrap::Vr41xxClockStrap;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    uint32_t Clksel() const override { return kClksel69MHz; }
};

}

REGISTER_SERVICE_AS(CompaqAero1500ClockStrap, Vr41xxClockStrap);

}
