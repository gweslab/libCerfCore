#pragma once

#include "../../execution/guest_cycle_clock.h"

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

namespace imx6_gic_detail {

class Imx6A9Timers {
public:
    void Attach(GuestCycleClock* clock, GuestCycleClock::Event* private_event,
                GuestCycleClock::Event* global_event);

    bool Read(uint32_t off, uint32_t& value);
    bool Write(uint32_t off, uint32_t value);
    uint32_t CatchUp();

    void Reset();
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void Resume();
    void SuspendForGatedClock();
    void ResumeFromGatedClock();

private:
    void     SetTickScale();
    void     TickScale(uint32_t control, uint64_t& num, uint64_t& den) const;
    uint64_t TicksAt(uint64_t cycles, uint64_t anchor, uint64_t phase,
                     uint64_t num, uint64_t den) const;
    uint64_t CyclesForTicks(uint64_t ticks, uint64_t phase,
                            uint64_t num, uint64_t den) const;
    uint32_t PrivateCountAt(uint64_t now) const;
    uint64_t PrivatePhaseAt(uint64_t now) const;
    void     SetPrivateNext(uint32_t count);
    uint64_t GlobalCountAt(uint64_t now) const;
    uint64_t GlobalPhaseAt(uint64_t now) const;
    void     RebasePrivate(uint64_t now, uint32_t count, uint64_t phase = 0u);
    void     RebaseGlobal(uint64_t now);
    void     ArmPrivate();
    void     ArmGlobal(uint64_t now);

    GuestCycleClock*        clock_         = nullptr;
    GuestCycleClock::Event* private_event_ = nullptr;
    GuestCycleClock::Event* global_event_  = nullptr;
    uint64_t                pt_tick_num_ = 1u;
    uint64_t                pt_tick_den_ = 1u;
    uint64_t                gt_tick_num_ = 1u;
    uint64_t                gt_tick_den_ = 1u;

    uint32_t pt_load_    = 0;
    uint32_t pt_control_ = 0;
    uint32_t pt_status_  = 0;
    uint32_t pt_count_   = 0;
    uint64_t pt_anchor_  = 0;
    uint64_t pt_phase_   = 0;
    uint64_t pt_next_    = 0;
    bool     pt_armed_   = false;

    uint64_t gt_count_     = 0;
    uint64_t gt_anchor_    = 0;
    uint64_t gt_phase_     = 0;
    uint32_t gt_control_   = 0;
    uint32_t gt_status_    = 0;
    uint64_t gt_compare_   = 0;
    uint32_t gt_increment_ = 0;
    bool     gt_fired_     = false;
    bool     clock_gated_  = false;
};

}

}
