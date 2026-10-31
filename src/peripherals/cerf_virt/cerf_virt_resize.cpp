#include <cerfcore/peripherals/cerf_virt/cerf_virt_resize.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_resize_regs.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>

#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

REGISTER_SERVICE(CerfVirtResize);

bool CerfVirtResize::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtResize::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t CerfVirtResize::MmioBase() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() + CerfVirt::kResizeOffset;
}
uint32_t CerfVirtResize::MmioSize() const { return CerfVirt::kResizeSize; }

uint32_t CerfVirtResize::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        case CerfVirt::kRszWantW:          return want_w_.load();
        case CerfVirt::kRszWantH:          return want_h_.load();
        case CerfVirt::kRszWantGen:        return want_gen_.load();
        case CerfVirt::kRszAppliedGen:     return applied_gen_.load();
        default:
            emu_.Get<Fatal>().Die("[CerfVirtResize] read +0x%X", addr - MmioBase());
    }
}

void CerfVirtResize::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - MmioBase()) {
        case CerfVirt::kRszAppliedW:       applied_w_.store(value); break;
        case CerfVirt::kRszAppliedH:       applied_h_.store(value); break;
        case CerfVirt::kRszAppliedQuarter: applied_quarter_.store(value); break;
        case CerfVirt::kRszAppliedGen: {
            applied_gen_.store(value);
            const uint32_t w = applied_w_.load();
            const uint32_t h = applied_h_.load();
            const uint32_t turn =
                emu_.Get<CerfVirtFramebuffer>().ApplyGuestMode(w, h, applied_quarter_.load());
            auto& presentation = emu_.Get<HostPresentation>();
            presentation.GuestRemoded(w, h);
            if (turn != 0u) presentation.GuestTurned(turn);
            break;
        }
        default:
            emu_.Get<Fatal>().Die("[CerfVirtResize] write +0x%X = 0x%08X",
                                  addr - MmioBase(), value);
    }
}

void CerfVirtResize::SaveState(StateWriter& w) {
    w.Write<uint32_t>("want_w", want_w_.load(std::memory_order_acquire));
    w.Write<uint32_t>("want_h", want_h_.load(std::memory_order_acquire));
    w.Write<uint32_t>("want_gen", want_gen_.load(std::memory_order_acquire));
    w.Write<uint32_t>("applied_w", applied_w_.load(std::memory_order_acquire));
    w.Write<uint32_t>("applied_h", applied_h_.load(std::memory_order_acquire));
    w.Write<uint32_t>("applied_gen", applied_gen_.load(std::memory_order_acquire));
    w.Write<uint32_t>("applied_quarter", applied_quarter_.load(std::memory_order_acquire));
}

void CerfVirtResize::RestoreState(StateReader& r) {
    uint32_t v;
    r.Read("want_w", v); want_w_.store(v, std::memory_order_release);
    r.Read("want_h", v); want_h_.store(v, std::memory_order_release);
    r.Read("want_gen", v); want_gen_.store(v, std::memory_order_release);
    r.Read("applied_w", v); applied_w_.store(v, std::memory_order_release);
    r.Read("applied_h", v); applied_h_.store(v, std::memory_order_release);
    r.Read("applied_gen", v); applied_gen_.store(v, std::memory_order_release);
    r.Read("applied_quarter", v); applied_quarter_.store(v, std::memory_order_release);
}

void CerfVirtResize::RequestResize(uint32_t w, uint32_t h) {
    want_w_.store(w);
    want_h_.store(h);
    want_gen_.fetch_add(1u);
}

}
