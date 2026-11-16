#include "cortex_m3_core.h"

#include <algorithm>

namespace cerfcore {

namespace cortex_m3 {

void Core::WaitForEvent(Bus& bus) {
    if (state_.event_register != 0u) {
        state_.event_register = 0u;
        return;
    }
    EnterSleep(bus, kSleepWfe);
}

void Core::WaitForInterrupt(Bus& bus) {
    EnterSleep(bus, kSleepWfi);
}

void Core::EnterSleep(Bus& bus, uint8_t kind) {
    if ((state_.scr & kScrSleepDeep) != 0u) {
        if (!bus.EnterDeepSleep()) return;
        state_.deep_sleep = 1u;
    }
    state_.sleep = kind;
}

bool Core::WakeupPending(bool wfi) const {
    const uint32_t best = HighestPending();
    return best != 0u && GroupPriority(best) < ExecutionPriority(!wfi);
}

bool Core::SleepEnds(Bus& bus) {
    const bool wfe = state_.sleep == kSleepWfe;
    if (!WakeupPending(!wfe) && !(wfe && state_.event_register != 0u)) return false;
    if (wfe) Raise(bus, "Cortex-M3: IMPLEMENTATION DEFINED Event Register state after a WFE suspension", state_.pc);
    state_.sleep = kSleepNone;
    if (state_.deep_sleep != 0u) {
        state_.deep_sleep = 0u;
        bus.ExitDeepSleep();
    }
    return true;
}

uint64_t Core::SysTickCyclesToPending() const {
    if ((state_.syst_csr & 3u) != 3u) return UINT64_MAX;
    uint64_t ticks = state_.syst_cvr;
    if (ticks == 0u) {
        if (state_.syst_rvr == 0u) return UINT64_MAX;
        ticks = 1u + state_.syst_rvr;
    }
    return (state_.syst_csr & 4u) != 0u ? ticks : ticks * 8u - state_.syst_prescale;
}

uint32_t Core::Idle() {
    uint64_t cycles = run_limit_ > state_.cycles ? run_limit_ - state_.cycles : 1u;
    if (state_.deep_sleep == 0u) cycles = std::min(cycles, SysTickCyclesToPending());
    const uint32_t advance = static_cast<uint32_t>(std::min(cycles, uint64_t{UINT32_MAX}));
    if (state_.deep_sleep == 0u) TickSysTick(advance);
    state_.cycles += advance;
    return advance;
}

}

}
