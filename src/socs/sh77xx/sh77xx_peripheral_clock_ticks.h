#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "../cycle_anchored_counter.h"
#include "../rated_tick_count.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;
class Sh77xxCpg;
class Sh77xxFrqcr;

class Sh77xxPeripheralClockTicks {
public:
    explicit Sh77xxPeripheralClockTicks(CerfEmulator& emu) : emu_(emu), ticks_(emu) {}

    void     Attach(std::function<void()> on_retime);
    uint64_t Ticks() const;
    uint64_t CycleOfTick(uint64_t tick) const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);
    void PostRestore();

private:
    void Retime();

    CerfEmulator&         emu_;
    GuestCycleClock*      clock_ = nullptr;
    Sh77xxFrqcr*          frqcr_ = nullptr;
    Sh77xxCpg*            cpg_   = nullptr;
    RatedTickCount        ticks_;
    GuestCycleClock::Rate rate_{};
    std::function<void()> on_retime_;
    uint64_t              restored_ticks_ = 0u;
    CounterPhase          restored_phase_{};
};

}
