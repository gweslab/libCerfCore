#include "engine_cycle_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../cpu/sh/sh_core_clock.h"
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include "engine_bridge.h"

namespace cerfcore {

namespace {

class ShGuestCycleClock : public EngineCycleClock<cerfengine::ShCpuState> {
public:
    using EngineCycleClock::EngineCycleClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kSh;
    }

    void OnReady() override {
        rate_ = emu_.Get<ShCoreClock>().ResetRate();
        LOG(Jit, "[CYCLECLK] SH reset rate %llu/%llu Hz\n", static_cast<unsigned long long>(rate_.num),
            static_cast<unsigned long long>(rate_.den));
        state_ = emu_.Get<EngineBridge>().Engine().Get<cerfengine::ShCpu>().State();
        GuestCycleClock::OnReady();
    }

protected:
    Rate InitialRate() override { return rate_; }

private:
    Rate rate_;
};

}

REGISTER_SERVICE_AS(ShGuestCycleClock, GuestCycleClock);

}
