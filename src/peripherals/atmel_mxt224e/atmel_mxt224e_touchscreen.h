#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "../touch/host_pen_target.h"
#include "atmel_mxt224e_map.h"

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class AtmelMxt224e;

class AtmelMxt224eTouchscreen : public Service, public HostPenTarget {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void ApplyPen(const HostPenInput::Pen& pen) override;
    void ResetPen() override;

    void TapTouchSpace(uint16_t x, uint16_t y);

    void ResetTouch();
    void ConfigChanged();
    void ReportAll();
    bool Sleeping();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    enum class TouchState : uint8_t { kFree, kDetecting, kDetected };

    struct Position {
        uint16_t x = 0u;
        uint16_t y = 0u;
    };

    struct Touch {
        uint16_t   count    = 0u;
        TouchState state    = TouchState::kFree;
        uint8_t    moved    = 0u;
        uint16_t   raw_x    = 0u;
        uint16_t   raw_y    = 0u;
        uint16_t   report_x = 0u;
        uint16_t   report_y = 0u;

        template <typename F>
        static constexpr void Visit(Touch& t, F& field) {
            field("mxt_touch_count", t.count);
            field("mxt_touch_state", t.state);
            field("mxt_touch_moved", t.moved);
            field("mxt_touch_raw_x", t.raw_x);
            field("mxt_touch_raw_y", t.raw_y);
            field("mxt_touch_report_x", t.report_x);
            field("mxt_touch_report_y", t.report_y);
        }
    };

    void     Acquire();
    bool     Down() const;
    void     Schedule();
    void     Arm(int64_t due_ns);
    bool     Busy() const;
    bool     FastDetectIntegration(const AtmelMxt224e& chip) const;
    bool     ActiveMode();
    int64_t  IntervalNs(const AtmelMxt224e& chip);
    Position Locate(const AtmelMxt224e& chip) const;
    bool     Jumped(const Touch& t, const Position& p, const AtmelMxt224e& chip) const;
    int      Continued(const Position& p, const AtmelMxt224e& chip) const;
    int      FreeSlot(const AtmelMxt224e& chip) const;
    void     Detect(Touch& t, uint8_t slot, const Position& p, AtmelMxt224e& chip);
    void     Follow(Touch& t, uint8_t slot, const Position& p, AtmelMxt224e& chip);
    void     Release(Touch& t, uint8_t slot, AtmelMxt224e& chip);
    void     Report(AtmelMxt224e& chip, uint8_t slot, uint8_t status, Position at) const;
    uint16_t ReleaseCycles(const AtmelMxt224e& chip) const;
    uint16_t DetectCycles(const AtmelMxt224e& chip, uint8_t slot) const;

    std::array<Touch, kMxtT9Touches> touches_{};
    GuestCycleClock::Event*           event_           = nullptr;
    int64_t                           due_ns_          = 0;
    int64_t                           active_until_ns_ = 0;
    int8_t                            tracked_         = -1;
    bool                              armed_           = false;
    bool                              host_down_       = false;
    bool                              tap_active_      = false;
    uint32_t                          tap_plate_x_     = 0u;
    uint32_t                          tap_plate_y_     = 0u;
    int64_t                           tap_until_ns_    = 0;
};

}
