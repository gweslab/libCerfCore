#include "cerf_virt_pointer.h"
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_pointer_regs.h>
#include "cerf_guest_liveness.h"

#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/host/pointer_input.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

REGISTER_SERVICE(CerfVirtPointer);

bool CerfVirtPointer::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtPointer::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t CerfVirtPointer::MmioBase() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() + CerfVirt::kPointerOffset;
}
uint32_t CerfVirtPointer::MmioSize() const { return CerfVirt::kPointerSize; }

uint32_t CerfVirtPointer::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        case CerfVirt::kPtrX:           return x_.load();
        case CerfVirt::kPtrY:           return y_.load();
        case CerfVirt::kPtrButtons: {
            std::lock_guard<std::mutex> lock(mutex_);
            const uint32_t              b = buttons_.load();
            change_unseen_              = false;
            if (!held_.empty()) {
                const PointerState next = held_.front();
                held_.pop_front();
                Publish(next);
            }
            return b;
        }
        case CerfVirt::kPtrWheelAccum:  return wheel_.load();
        case CerfVirt::kPtrSeq:         return seq_.load();
        case CerfVirt::kPtrAttach: {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!held_.empty()) {
                Publish(held_.back());
                held_.clear();
            }
            change_unseen_ = false;
            return seq_.load();
        }
        default:                        return 0u;
    }
}

void CerfVirtPointer::WriteWord(uint32_t, uint32_t) {}

void CerfVirtPointer::SaveState(StateWriter& w) {
    w.Write<uint32_t>("x", x_.load());
    w.Write<uint32_t>("y", y_.load());
    w.Write<uint32_t>("buttons", buttons_.load());
    w.Write<uint32_t>("wheel", wheel_.load());
    w.Write<uint32_t>("seq", seq_.load());
}

void CerfVirtPointer::RestoreState(StateReader& r) {
    uint32_t v;
    r.Read("x", v); x_.store(v);
    r.Read("y", v); y_.store(v);
    r.Read("buttons", v); buttons_.store(v);
    r.Read("wheel", v); wheel_.store(v);
    r.Read("seq", v); seq_.store(v);
    std::lock_guard<std::mutex> lock(mutex_);
    held_.clear();
    change_unseen_ = false;
    if (buttons_.load() != 0u) Publish({x_.load(), y_.load(), 0u});
}

void CerfVirtPointer::Bump() { seq_.fetch_add(1u); }

void CerfVirtPointer::Publish(const PointerState& state) {
    if (buttons_.load() != state.buttons) change_unseen_ = true;
    x_.store(state.x);
    y_.store(state.y);
    buttons_.store(state.buttons);
    Bump();
}

void CerfVirtPointer::SetPointer(uint32_t nx, uint32_t ny, uint32_t buttons) {
    std::lock_guard<std::mutex> lock(mutex_);
    Hold({nx, ny, buttons});
}

void CerfVirtPointer::Hold(const PointerState& next) {
    if (held_.empty() && !change_unseen_) {
        Publish(next);
        return;
    }
    if (!held_.empty() && held_.back().buttons == next.buttons) {
        held_.back() = next;
        return;
    }
    if (held_.size() == kHeldLimit) held_.pop_front();
    held_.push_back(next);
}

void CerfVirtPointer::AddWheel(int delta) {
    wheel_.fetch_add((uint32_t)delta);
    Bump();
}

void CerfVirtPointer::ClearButtons() {
    std::lock_guard<std::mutex> lock(mutex_);
    const PointerState last = held_.empty() ? PointerState{x_.load(), y_.load(), buttons_.load()} : held_.back();
    Hold({last.x, last.y, 0u});
}

namespace {

class CerfVirtPointerInput : public PointerInput {
public:
    using PointerInput::PointerInput;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnMove(int sx, int sy, uint32_t button_mask) override {
        uint32_t nx, ny;
        Normalize(sx, sy, nx, ny);
        emu_.Get<CerfVirtPointer>().SetPointer(nx, ny, button_mask);
    }

    void OnWheel(int sx, int sy, int delta) override {
        (void)sx; (void)sy;
        emu_.Get<CerfVirtPointer>().AddWheel(delta);
    }

    void OnCaptureLost() override {
        emu_.Get<CerfVirtPointer>().ClearButtons();
    }

    bool SourceReady() const override {
        return emu_.Get<CerfGuestLiveness>().IsAlive();
    }

private:
    void Normalize(int sx, int sy, uint32_t& nx, uint32_t& ny) {
        auto& hc = emu_.Get<HostPresentation>();
        int w = (int)hc.SurfaceWidth();
        int h = (int)hc.SurfaceHeight();
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (w > 0 && sx > w - 1) sx = w - 1;
        if (h > 0 && sy > h - 1) sy = h - 1;
        nx = (w > 1) ? (uint32_t)((uint64_t)sx * 65535u / (uint32_t)(w - 1)) : 0u;
        ny = (h > 1) ? (uint32_t)((uint64_t)sy * 65535u / (uint32_t)(h - 1)) : 0u;
    }
};

REGISTER_SERVICE_AS(CerfVirtPointerInput, PointerInput);

}

}
