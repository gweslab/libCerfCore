#include "omap3530_cm_mcbsp_gate_block.h"

#include <cerfcore/socs/guest_cpu_reset.h>

#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kOffFclken = 0x00u;
constexpr uint32_t kOffIclken = 0x10u;

}

void Omap3530CmMcBspGateBlock::OnReady() {
    Omap3530PrcmStubBlock::OnReady();
    {
        std::lock_guard<std::mutex> lk(mu_);
        LoadResetLocked();
    }
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        if (!reset_changed_) return;
        reset_changed_ = false;
        NotifyClock();
    });
}

void Omap3530CmMcBspGateBlock::AfterResetLocked(const std::vector<uint32_t>& before) {
    const bool other = LoadResetLocked();
    reset_changed_   = other || McBspGatesLocked() != GatesOf(before);
}

uint64_t Omap3530CmMcBspGateBlock::GatesOf(const std::vector<uint32_t>& regs) const {
    const uint32_t mask = McBspMask();
    return (regs[kOffFclken / 4u] & mask) | (static_cast<uint64_t>(regs[kOffIclken / 4u] & mask) << 32);
}

uint64_t Omap3530CmMcBspGateBlock::McBspGatesLocked() const { return GatesOf(regs_); }

uint64_t Omap3530CmMcBspGateBlock::McBspGates() const {
    std::lock_guard<std::mutex> lk(mu_);
    return McBspGatesLocked();
}

void Omap3530CmMcBspGateBlock::WriteWord(uint32_t addr, uint32_t value) {
    const uint64_t before = McBspGates();
    Omap3530PrcmStubBlock::WriteWord(addr, value);
    if (McBspGates() != before) NotifyClock();
}

void Omap3530CmMcBspGateBlock::WriteHalf(uint32_t addr, uint16_t value) {
    const uint64_t before = McBspGates();
    Omap3530PrcmStubBlock::WriteHalf(addr, value);
    if (McBspGates() != before) NotifyClock();
}

bool Omap3530CmMcBspGateBlock::McBspFclkEnabled(uint32_t port) const {
    return (PeekReg(kOffFclken) & McBspBit(port)) != 0u;
}

bool Omap3530CmMcBspGateBlock::McBspIclkEnabled(uint32_t port) const {
    return (PeekReg(kOffIclken) & McBspBit(port)) != 0u;
}

void Omap3530CmMcBspGateBlock::RegisterClockListener(std::function<void()> fn) {
    clock_listeners_.Add(std::move(fn));
}

void Omap3530CmMcBspGateBlock::NotifyClock() {
    clock_listeners_.Call();
}

}
