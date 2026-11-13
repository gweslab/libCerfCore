#pragma once

#include <cerfcore/host/touch_input.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/guest_sample_clock.h"
#include "host_pen_input.h"

#include <cstdint>

namespace cerfcore {

class GuestClockTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    using PanelRange = HostPenInput::PanelRange;

    void OnReady() final;

    void OnPenDown(int x, int y) final;
    void OnPenMove(int x, int y) final;
    void OnPenUp(int x, int y) final;
    void OnCaptureLost() final;

protected:
    using HostPen = HostPenInput::Pen;
    static constexpr uint8_t kPenUp          = HostPenInput::kPenUp;
    static constexpr uint8_t kPenDown        = HostPenInput::kPenDown;
    static constexpr uint8_t kFirstOwnerKind = HostPenInput::kFirstOwnerKind;

    virtual void OnTouchReady() = 0;
    virtual void ApplyHostPen(const HostPen& pen) = 0;
    virtual void OnSample() = 0;

    void QueueHostPen(const HostPen& pen) { pens_.Queue(pen); }
    void LatestPosition(int* x, int* y) {
        const HostPenInput::Point p = pens_.Latest();
        *x = p.x;
        *y = p.y;
    }
    void MapToPanel(int x, int y, const PanelRange& range, uint16_t* adc_x, uint16_t* adc_y) const {
        pens_.MapToPanel(x, y, range, adc_x, adc_y);
    }

    void StartSamples(GuestCycleClock::Rate rate);
    void SetSampleRate(GuestCycleClock::Rate rate);
    void StopSamples();
    void ClearOnRestore();

private:
    HostPenInput     pens_;
    GuestSampleClock samples_{emu_};
};

}
