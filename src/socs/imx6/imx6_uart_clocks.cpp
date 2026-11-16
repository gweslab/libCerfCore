#include "../freescale_uart_clocks.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx6_ccm.h"
#include "imx6_id.h"

namespace cerfcore {

namespace {

class Imx6UartClocks : public FreescaleUartClocks {
public:
    using FreescaleUartClocks::FreescaleUartClocks;

    bool ShouldRegister() override {
        return SocId::IsImx6(emu_.Get<BoardContext>().GetSocId());
    }

    void OnReady() override { ccm_ = &emu_.Get<Imx6Ccm>(); }

    GuestCycleClock::Rate ModuleClockRate() const override { return ccm_->UartClockRootRate(); }

    void RegisterRateListener(std::function<void()> fn) override {
        ccm_->RegisterRateListener(std::move(fn));
    }

private:
    Imx6Ccm* ccm_ = nullptr;
};

}

REGISTER_SERVICE_AS(Imx6UartClocks, FreescaleUartClocks);

}
