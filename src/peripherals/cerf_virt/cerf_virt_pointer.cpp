#include "cerf_virt_pointer.h"
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_pointer_regs.h>
#include "cerf_guest_liveness.h"

#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/host/pointer_input.h>
#include <cerfcore/host/pointer_router.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

REGISTER_SERVICE(CerfVirtPointer);

bool CerfVirtPointer::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtPointer::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (!emu_.Get<GuestCpuReset>().DeliveredResetWasResume())
            no_injector_.store(false, std::memory_order_release);
    });
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
            if (!held_.Empty()) Publish(held_.Pop());
            return b;
        }
        case CerfVirt::kPtrWheelAccum:  return wheel_.load();
        case CerfVirt::kPtrSeq:         return seq_.load();
        case CerfVirt::kPtrAttach: {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!held_.Empty()) {
                Publish(held_.Back());
                held_.Clear();
            }
            change_unseen_ = false;
            return seq_.load();
        }
        default:
            emu_.Get<Fatal>().Die("[CerfVirtPointer] read +0x%X", addr - MmioBase());
    }
}

void CerfVirtPointer::WriteWord(uint32_t addr, uint32_t value) {
    if (addr - MmioBase() != CerfVirt::kPtrNoInjector)
        emu_.Get<Fatal>().Die("[CerfVirtPointer] write +0x%X = 0x%08X", addr - MmioBase(), value);
    no_injector_.store(value != 0u, std::memory_order_release);
    LOG(GuestAdditions, "guest additions: the guest %s pointer input\n",
        value != 0u ? "cannot inject" : "injects");
    emu_.Get<PointerRouter>().ReevaluateAuto();
}

void CerfVirtPointer::SaveState(StateWriter& w) {
    w.Write<uint32_t>("x", x_.load());
    w.Write<uint32_t>("y", y_.load());
    w.Write<uint32_t>("buttons", buttons_.load());
    w.Write<uint32_t>("wheel", wheel_.load());
    w.Write<uint32_t>("seq", seq_.load());
    w.Write<uint8_t>("no_injector", no_injector_.load(std::memory_order_acquire) ? 1u : 0u);
}

void CerfVirtPointer::RestoreState(StateReader& r) {
    uint32_t v;
    r.Read("x", v); x_.store(v);
    r.Read("y", v); y_.store(v);
    r.Read("buttons", v); buttons_.store(v);
    r.Read("wheel", v); wheel_.store(v);
    r.Read("seq", v); seq_.store(v);
    uint8_t no_injector = 0;
    r.Read("no_injector", no_injector);
    no_injector_.store(no_injector != 0u, std::memory_order_release);
    std::lock_guard<std::mutex> lock(mutex_);
    held_.Clear();
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
    if (held_.Empty() && !change_unseen_) {
        Publish(next);
        return;
    }
    if (!held_.Empty() && held_.Back().buttons == next.buttons) {
        held_.Back() = next;
        return;
    }
    held_.Push(next);
}

void CerfVirtPointer::AddWheel(int delta) {
    wheel_.fetch_add((uint32_t)delta);
    Bump();
}

void CerfVirtPointer::ClearButtons() {
    std::lock_guard<std::mutex> lock(mutex_);
    const PointerState last = held_.Empty() ? PointerState{x_.load(), y_.load(), buttons_.load()} : held_.Back();
    Hold({last.x, last.y, 0u});
}

namespace {

class CerfVirtPointerInput : public PointerInput {
public:
    using PointerInput::PointerInput;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    bool SourceReady() const override {
        return emu_.Get<CerfGuestLiveness>().IsAlive() && emu_.Get<CerfVirtPointer>().GuestInjects();
    }

protected:
    void Move(int sx, int sy, uint32_t button_mask) override {
        uint32_t nx, ny;
        Normalize(sx, sy, nx, ny);
        emu_.Get<CerfVirtPointer>().SetPointer(nx, ny, button_mask);
    }

    void Wheel(int sx, int sy, int delta) override {
        (void)sx; (void)sy;
        emu_.Get<CerfVirtPointer>().AddWheel(delta);
    }

    void CaptureLost() override {
        emu_.Get<CerfVirtPointer>().ClearButtons();
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
