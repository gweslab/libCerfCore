#include "cortex_m3_core.h"

#include <algorithm>

namespace cerfcore {

namespace cortex_m3 {

namespace {

constexpr uint32_t kExceptionEntryCycles = 12u;
constexpr uint32_t kLateArrivalCycles = 8u;

}

uint32_t Core::ArrivalWithin() const {
    const uint64_t to_limit = run_limit_ > state_.cycles ? run_limit_ - state_.cycles : 0u;
    return committed_ + static_cast<uint32_t>(std::min<uint64_t>({to_limit, SysTickCyclesToPending(), UINT32_MAX / 2u}));
}

bool Core::PendingAt(Bus& bus, uint32_t elapsed) {
    const uint32_t advance = elapsed - committed_;
    TickSysTick(advance);
    state_.cycles += advance;
    committed_ = elapsed;
    run_limit_ = std::min(run_target_, bus.Synchronize());
    return PendingToTake() != 0u;
}

bool Core::InterruptedDuring(Bus& bus, uint32_t cost) {
    cycles_ = cost;
    for (uint32_t arrival = ArrivalWithin(); arrival < cost; arrival = ArrivalWithin()) {
        if (PendingAt(bus, arrival)) {
            cycles_ = arrival + 1u;
            state_.pc = this_instr_;
            branched_ = true;
            exception_event_ = true;
            return true;
        }
        if (ArrivalWithin() <= arrival) break;
    }
    return false;
}

uint32_t Core::LateArrivals(Bus& bus, uint32_t current, uint32_t cost, uint32_t latency) {
    for (uint32_t arrival = ArrivalWithin(); arrival < cost; arrival = ArrivalWithin()) {
        if (!PendingAt(bus, arrival)) {
            if (ArrivalWithin() <= arrival) break;
            continue;
        }
        const uint32_t later = PendingToTake();
        state_.active[current] = 0u;
        state_.pending[current] = 1u;
        ExceptionTaken(bus, later);
        current = later;
        cost = std::max(cost, arrival + latency);
    }
    return cost;
}

uint32_t Core::TakePendingException(Bus& bus, uint32_t exception) {
    state_.load_regs = 0u;
    if (state_.sleep_exc_return != 0u) {
        state_.lr = 0xF0000000u | state_.sleep_exc_return;
        state_.sleep_exc_return = 0u;
        ExceptionTaken(bus, exception);
        return LateArrivals(bus, exception, kTailChainCycles, kTailChainCycles);
    }
    ExceptionEntry(bus, exception);
    return LateArrivals(bus, exception, kExceptionEntryCycles, kLateArrivalCycles);
}

bool Core::TailChained(Bus& bus, uint32_t exc_return, uint32_t pop_cycles) {
    if (ArrivalWithin() <= cycles_) PendingAt(bus, cycles_);
    uint32_t chained = ComputePendingToTake();
    uint32_t start = cycles_;
    for (uint32_t arrival = ArrivalWithin(); chained == 0u && arrival < cycles_ + pop_cycles; arrival = ArrivalWithin()) {
        if (PendingAt(bus, arrival)) {
            chained = ComputePendingToTake();
            start = arrival;
        } else if (ArrivalWithin() <= arrival) {
            break;
        }
    }
    if (chained == 0u) return false;
    state_.lr = 0xF0000000u | exc_return;
    ExceptionTaken(bus, chained);
    cycles_ = LateArrivals(bus, chained, start + kTailChainCycles, kTailChainCycles);
    branched_ = true;
    return true;
}

}

}
