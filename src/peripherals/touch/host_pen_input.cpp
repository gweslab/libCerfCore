#include "host_pen_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_presentation.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/host_request_channel.h>

#include <utility>

namespace cerfcore {

namespace {

uint16_t MapAxis(int v, int v_max, uint16_t lo, uint16_t hi) {
    if (v_max <= 0) return lo;
    if (v <= 0)     return lo;
    if (v >= v_max) return hi;
    return static_cast<uint16_t>(lo + static_cast<int64_t>(v) * (hi - lo) / v_max);
}

}

void HostPenInput::Attach(CerfEmulator& emu, std::function<void(const Pen&)> apply) {
    emu_      = &emu;
    apply_    = std::move(apply);
    requests_ = &emu.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
    requests_->RegisterListener([this] { Apply(); });
}

void HostPenInput::Queue(const Pen& pen) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pens_.push_back(pen);
    }
    requests_->Request();
}

void HostPenInput::Track(int x, int y) {
    const uint64_t packed = static_cast<uint64_t>(static_cast<uint32_t>(x)) |
                            (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32);
    latest_.store(packed, std::memory_order_relaxed);
}

HostPenInput::Point HostPenInput::Latest() const {
    const uint64_t packed = latest_.load(std::memory_order_relaxed);
    return Point{static_cast<int>(static_cast<uint32_t>(packed)), static_cast<int>(static_cast<uint32_t>(packed >> 32))};
}

void HostPenInput::MapToPanel(int x, int y, const PanelRange& range, uint16_t* adc_x,
                              uint16_t* adc_y) const {
    auto&     hc = emu_->Get<HostPresentation>();
    const int w  = (int)hc.SurfaceWidth();
    const int h  = (int)hc.SurfaceHeight();
    *adc_x = MapAxis(x, w - 1, range.x_lo, range.x_hi);
    *adc_y = MapAxis(y, h - 1, range.y_lo, range.y_hi);
}

void HostPenInput::Apply() {
    std::vector<Pen> pens;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pens.swap(pens_);
    }
    for (const Pen& pen : pens) apply_(pen);
}

void HostPenInput::Clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    pens_.clear();
}

}
