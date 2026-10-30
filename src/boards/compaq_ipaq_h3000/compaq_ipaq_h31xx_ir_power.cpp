#include "compaq_ipaq_h3000_ir_power.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include "ipaq_h31xx_id.h"

#include <atomic>
#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kIrOn = 1u << 8;

class CompaqIpaqH31xxIrPower : public CompaqIpaqH3000IrPower {
public:
    using CompaqIpaqH3000IrPower::CompaqIpaqH3000IrPower;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardId() == BoardId::IpaqH31xx;
    }

    void OnReady() override {
        emu_.Get<Sa11xxGpio>().RegisterOutputObserver(
            [this](uint32_t levels, uint32_t out_mask, bool) {
                powered_.store((levels & out_mask & kIrOn) != 0u, std::memory_order_release);
            });
    }

    bool Powered() const override { return powered_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> powered_{false};
};

}

REGISTER_SERVICE_AS(CompaqIpaqH31xxIrPower, CompaqIpaqH3000IrPower);

}
