#pragma once

#include "../execution/guest_cycle_clock.h"
#include "rated_tick_count.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

class GuestSampleClock {
public:
    explicit GuestSampleClock(CerfEmulator& emu) : emu_(emu), samples_(emu) {}

    void Attach(std::function<void()> on_sample);
    void Start(GuestCycleClock::Rate rate);
    void SetRate(GuestCycleClock::Rate rate);
    void Stop();

    void Save(StateWriter& w, const char* prefix) const;
    void Restore(StateReader& r, const char* prefix);

private:
    void OnEvent();
    void Rescale();
    void ArmNext();

    CerfEmulator&           emu_;
    std::function<void()>   on_sample_;
    GuestCycleClock*        clock_    = nullptr;
    GuestCycleClock::Event* event_    = nullptr;
    RatedTickCount          samples_;
    GuestCycleClock::Rate   rate_{};
    uint64_t                next_     = 0;
    bool                    sampling_ = false;
};

}
