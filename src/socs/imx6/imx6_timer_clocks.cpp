#include "../freescale_timer_clocks.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "imx6_ccm.h"
#include "imx6_id.h"


namespace cerfcore {

namespace {

class Imx6TimerClocks : public FreescaleTimerClocks {
public:
    using FreescaleTimerClocks::FreescaleTimerClocks;

    bool ShouldRegister() override {
        return SocId::IsImx6(emu_.Get<BoardContext>().GetSocId());
    }

    void OnReady() override { ccm_ = &emu_.Get<Imx6Ccm>(); }

    GuestCycleClock::Rate InputRate(FreescaleTimerUnit unit, FreescaleTimerInput) const override {
        NoTimerUnit(unit);
    }

    bool InputRunsIn(FreescaleTimerUnit unit, FreescaleTimerInput,
                     FreescaleLowPowerMode) const override {
        NoTimerUnit(unit);
    }

    FreescaleLowPowerMode WfiMode() const override {
        switch (ccm_->WfiLowPowerMode()) {
            case 0u: return FreescaleLowPowerMode::kRun;
            case 1u: return FreescaleLowPowerMode::kWait;
            case 2u: return FreescaleLowPowerMode::kStop;
            default: break;
        }
        emu_.Get<Fatal>().Die("Imx6TimerClocks: CLPCR selects the reserved LPM 11");
    }

    void RegisterRateListener(std::function<void()>) override {
        emu_.Get<Fatal>().Die("Imx6TimerClocks: no i.MX6 EPIT or GPT is modelled to take a "
                              "rate listener");
    }

private:
    [[noreturn]] void NoTimerUnit(FreescaleTimerUnit unit) const {
        emu_.Get<Fatal>().Die("Imx6TimerClocks: timer unit %u is not modelled on i.MX6",
                              static_cast<unsigned>(unit));
    }

    Imx6Ccm* ccm_ = nullptr;
};

}

REGISTER_SERVICE_AS(Imx6TimerClocks, FreescaleTimerClocks);

}
