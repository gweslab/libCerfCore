#include "msm8255_saw.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/engine_bridge.h"
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfengine/arm/arm_jit.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>

namespace cerfcore {

REGISTER_SERVICE(Msm8255Saw);

namespace {

/* Linux arch/arm/mach-msm msm_iomap-7x30.h: MSM_SAW_PHYS, MSM_SAW_SIZE. */
constexpr uint32_t kSawBase = 0xC0102000u;
constexpr uint32_t kSawSize = 0x00001000u;

/* Linux arch/arm/mach-msm spm.c msm_spm_reg_offsets: MSM_SPM_REG_SAW_CFG 0x10
   through MSM_SPM_REG_SAW_SPM_MPM_CFG 0x38, one word apart. */
constexpr uint32_t kRegFileFirst = 0x10u;
constexpr uint32_t kRegFileLast  = 0x38u;

constexpr uint32_t kReg04 = 0x04u;

constexpr uint32_t kRegVctl          = 0x08u;
constexpr uint32_t kRegSts           = 0x0Cu;
constexpr uint32_t kVctlLevelMask    = 0xFFu;
constexpr uint32_t kStsPmicDataShift = 10u;

constexpr uint32_t kRegReset = 0u;

constexpr uint32_t kRegSpmCtl          = 0x14u;
constexpr uint32_t kRegSlpRstEn        = 0x34u;
constexpr uint32_t kSpmModeMask        = 0x7u;
constexpr uint32_t kSpmModeClockGating = 0u;
constexpr uint32_t kSpmModeSleep       = 2u;
constexpr uint32_t kSlpRstEnable       = 1u;

static_assert(Msm8255Saw::kRegFileWords == (kRegFileLast - kRegFileFirst) / 4u + 1u);

}

bool Msm8255Saw::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
}

void Msm8255Saw::OnReady() {
    jit_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { Reset(); });
    emu_.Get<GuestCycleClock>().RegisterIdleListener([this] { OnIdle(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Msm8255Saw::MmioBase() const { return kSawBase; }
uint32_t Msm8255Saw::MmioSize() const { return kSawSize; }

uint32_t Msm8255Saw::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off == kReg04) return reg04_.load(std::memory_order_acquire);
    if (off == kRegVctl) return vctl_.load(std::memory_order_acquire);
    if (off == kRegSts) {
        return (vctl_.load(std::memory_order_acquire) & kVctlLevelMask) << kStsPmicDataShift;
    }
    if (InRegFile(off)) {
        return regs_[RegIndex(off)].load(std::memory_order_acquire);
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Msm8255Saw::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off == kReg04) {
        reg04_.store(value, std::memory_order_release);
        return;
    }
    if (off == kRegVctl && (value & ~kVctlLevelMask) == 0u) {
        vctl_.store(value, std::memory_order_release);
        return;
    }
    if (InRegFile(off)) {
        regs_[RegIndex(off)].store(value, std::memory_order_release);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

bool Msm8255Saw::TakeCoreCollapse() {
    const bool collapsed = core_collapsed_;
    core_collapsed_      = false;
    RequestPowerUp(false);
    return collapsed;
}

void Msm8255Saw::CollapseCore() {
    core_collapsed_ = true;
}

void Msm8255Saw::RequestPowerUp(bool requested) {
    power_up_requested_ = requested;
    jit_->SetIdleWake(requested);
}

void Msm8255Saw::OnIdle() {
    const uint32_t mode = regs_[RegIndex(kRegSpmCtl)].load(std::memory_order_acquire) & kSpmModeMask;
    if (mode != kSpmModeClockGating && mode != kSpmModeSleep) {
        emu_.Get<Fatal>().Die("Msm8255Saw: WFI with SPM mode %u", mode);
    }
    const uint32_t rst = regs_[RegIndex(kRegSlpRstEn)].load(std::memory_order_acquire);
    if (mode == kSpmModeSleep && (rst & kSlpRstEnable) != 0u) core_collapsed_ = true;
}

void Msm8255Saw::Reset() {
    reg04_.store(kRegReset, std::memory_order_release);
    vctl_.store(kRegReset, std::memory_order_release);
    for (uint32_t i = 0; i < kRegFileWords; ++i) {
        regs_[i].store(kRegReset, std::memory_order_release);
    }
    core_collapsed_ = false;
    RequestPowerUp(false);
}

void Msm8255Saw::SaveState(StateWriter& w) {
    w.Write<uint32_t>("reg04", reg04_.load(std::memory_order_acquire));
    for (uint32_t i = 0; i < kRegFileWords; ++i) {
        w.Write<uint32_t>("regs", regs_[i].load(std::memory_order_acquire));
    }
    w.Write<uint32_t>("vctl", vctl_.load(std::memory_order_acquire));
    w.Write<uint32_t>("saw_core_collapsed", core_collapsed_ ? 1u : 0u);
    w.Write<uint32_t>("saw_power_up_requested", power_up_requested_ ? 1u : 0u);
}

void Msm8255Saw::RestoreState(StateReader& r) {
    uint32_t reg04 = kRegReset;
    r.Read("reg04", reg04);
    reg04_.store(reg04, std::memory_order_release);
    for (uint32_t i = 0; i < kRegFileWords; ++i) {
        uint32_t v = kRegReset;
        r.Read("regs", v);
        regs_[i].store(v, std::memory_order_release);
    }
    uint32_t vctl = kRegReset;
    r.Read("vctl", vctl);
    vctl_.store(vctl, std::memory_order_release);
    uint32_t collapsed = 0u;
    r.Read("saw_core_collapsed", collapsed);
    core_collapsed_ = collapsed != 0u;
    uint32_t power_up = 0u;
    r.Read("saw_power_up_requested", power_up);
    power_up_requested_ = power_up != 0u;
}

void Msm8255Saw::PostRestore() {
    jit_->SetIdleWake(power_up_requested_);
}

bool Msm8255Saw::InRegFile(uint32_t off) {
    return off >= kRegFileFirst && off <= kRegFileLast && (off & 3u) == 0u;
}

uint32_t Msm8255Saw::RegIndex(uint32_t off) {
    return (off - kRegFileFirst) / 4u;
}

}
