#include "guest_clock_touch_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_presentation.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/host_request_channel.h>

namespace cerfcore {

namespace {

uint16_t MapAxis(int v, int v_max, uint16_t lo, uint16_t hi) {
    if (v_max <= 0) return lo;
    if (v <= 0)     return lo;
    if (v >= v_max) return hi;
    return static_cast<uint16_t>(lo + static_cast<int64_t>(v) * (hi - lo) / v_max);
}

}

void GuestClockTouchInput::OnReady() {
    samples_.Attach([this] { OnSample(); });
    host_requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
    host_requests_->RegisterListener([this] { OnHostRequest(); });
    OnTouchReady();
}

void GuestClockTouchInput::OnPenDown(int x, int y) {
    QueueHostPen(HostPen{kPenDown, x, y});
}

void GuestClockTouchInput::OnPenMove(int x, int y) {
    std::lock_guard<std::mutex> lk(host_mtx_);
    last_x_ = x;
    last_y_ = y;
}

void GuestClockTouchInput::OnPenUp(int, int) {
    QueueHostPen(HostPen{kPenUp, 0, 0});
}

void GuestClockTouchInput::OnCaptureLost() {
    QueueHostPen(HostPen{kPenUp, 0, 0});
}

void GuestClockTouchInput::QueueHostPen(const HostPen& pen) {
    {
        std::lock_guard<std::mutex> lk(host_mtx_);
        if (pen.kind == kPenDown) {
            last_x_ = pen.x;
            last_y_ = pen.y;
        }
        host_pens_.push_back(pen);
    }
    host_requests_->Request();
}

void GuestClockTouchInput::OnHostRequest() {
    std::vector<HostPen> pens;
    {
        std::lock_guard<std::mutex> lk(host_mtx_);
        pens.swap(host_pens_);
    }
    for (const HostPen& pen : pens) ApplyHostPen(pen);
}

void GuestClockTouchInput::LatestPosition(int* x, int* y) {
    std::lock_guard<std::mutex> lk(host_mtx_);
    *x = last_x_;
    *y = last_y_;
}

void GuestClockTouchInput::MapToPanel(int x, int y, const PanelRange& range, uint16_t* adc_x,
                                      uint16_t* adc_y) const {
    auto&     hc = emu_.Get<HostPresentation>();
    const int w  = (int)hc.SurfaceWidth();
    const int h  = (int)hc.SurfaceHeight();
    *adc_x = MapAxis(x, w - 1, range.x_lo, range.x_hi);
    *adc_y = MapAxis(y, h - 1, range.y_lo, range.y_hi);
}

void GuestClockTouchInput::StartSamples(GuestCycleClock::Rate rate) {
    samples_.Start(rate);
}

void GuestClockTouchInput::SetSampleRate(GuestCycleClock::Rate rate) {
    samples_.SetRate(rate);
}

void GuestClockTouchInput::StopSamples() {
    samples_.Stop();
}

void GuestClockTouchInput::ClearOnRestore() {
    StopSamples();
    std::lock_guard<std::mutex> lk(host_mtx_);
    host_pens_.clear();
}

}
