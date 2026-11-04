#pragma once

#include <cerfcore/host/touch_input.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/guest_sample_clock.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace cerfengine { class HostRequestChannel; }

namespace cerfcore {

class GuestClockTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    struct PanelRange {
        uint16_t x_lo;
        uint16_t x_hi;
        uint16_t y_lo;
        uint16_t y_hi;
    };

    void OnReady() final;

    void OnPenDown(int x, int y) final;
    void OnPenMove(int x, int y) final;
    void OnPenUp(int x, int y) final;
    void OnCaptureLost() final;

protected:
    struct HostPen {
        uint8_t kind;
        int     x;
        int     y;
    };
    static constexpr uint8_t kPenUp          = 0u;
    static constexpr uint8_t kPenDown        = 1u;
    static constexpr uint8_t kFirstOwnerKind = 2u;

    virtual void OnTouchReady() = 0;
    virtual void ApplyHostPen(const HostPen& pen) = 0;
    virtual void OnSample() = 0;

    void QueueHostPen(const HostPen& pen);
    void LatestPosition(int* x, int* y);
    void MapToPanel(int x, int y, const PanelRange& range, uint16_t* adc_x,
                    uint16_t* adc_y) const;

    void StartSamples(GuestCycleClock::Rate rate);
    void SetSampleRate(GuestCycleClock::Rate rate);
    void StopSamples();
    void ClearOnRestore();

private:
    void OnHostRequest();

    std::mutex           host_mtx_;
    std::vector<HostPen> host_pens_;
    int                  last_x_ = 0;
    int                  last_y_ = 0;

    cerfengine::HostRequestChannel* host_requests_ = nullptr;
    GuestSampleClock                samples_{emu_};
};

}
