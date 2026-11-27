#include "arm_performance_monitors.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/engine_bridge.h"
#include "../../socs/irq_controller.h"
#include "../arm_processor_config.h"

#include <cerfengine/arm/arm_jit.h>

#include <algorithm>
#include <cstdint>

namespace cerfcore {

REGISTER_SERVICE(ArmPerformanceMonitors);

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

constexpr uint32_t kEnable             = 1u << 0;
constexpr uint32_t kEventCounterReset  = 1u << 1;
constexpr uint32_t kCycleCounterReset  = 1u << 2;
constexpr uint32_t kCycleDivider       = 1u << 3;
constexpr uint32_t kControlWritable    = 0x39u;
constexpr uint32_t kIdentificationMask = 0xFFFFF800u;
constexpr uint32_t kCountersShift      = 11u;
constexpr uint32_t kCountersMask       = 0x1Fu;
constexpr uint32_t kCycleCounter       = 31u;
constexpr uint32_t kCycleCounterBit    = 1u << kCycleCounter;
constexpr uint32_t kSelectionMask      = 0x1Fu;
constexpr uint32_t kEventTypeMask      = 0xFFu;
constexpr uint32_t kUserEnable         = 1u;

constexpr uint32_t kSoftwareIncrementEvent = 0x00u;
constexpr uint32_t kCpuCyclesEvent         = 0x11u;

constexpr uint64_t kCycleDividerTicks = 64u;
constexpr uint64_t kCounterWrap       = 1ull << 32;
constexpr uint64_t kNeverDue          = UINT64_MAX;

constexpr uint32_t kControlCrm   = 12u;
constexpr uint32_t kCounterCrm   = 13u;
constexpr uint32_t kInterruptCrm = 14u;

ArmPerformanceMonitors& Self(void* self) {
    return *static_cast<ArmPerformanceMonitors*>(self);
}

}

bool ArmPerformanceMonitors::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kArm &&
           emu_.Get<ArmProcessorConfig>().HasPerformanceMonitors();
}

void ArmPerformanceMonitors::OnReady() {
    const ArmProcessorConfig& config = emu_.Get<ArmProcessorConfig>();
    identification_ = config.PmcrIdentification() & kIdentificationMask;
    event_counters_ = (identification_ >> kCountersShift) & kCountersMask;
    interrupt_line_ = config.PmuInterrupt();
    clock_          = &emu_.Get<GuestCycleClock>();
    overflow_event_ = clock_->Add([this] {
        Sync();
        Schedule();
        DriveIrq(false);
    });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { Reset(); });
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] {
        Rebase();
        Schedule();
        DriveIrq(true);
    });
}

ArmCoprocDecision ArmPerformanceMonitors::Decide(const ArmCoprocAccess& a) {
    if (a.opc1 != 0u) return {.action = A::kUndefined};
    switch (a.crm) {
    case kControlCrm:   return ControlGroup(a);
    case kCounterCrm:   return CounterGroup(a);
    case kInterruptCrm: return InterruptGroup(a);
    default:            return {.action = A::kUndefined};
    }
}

ArmCoprocDecision ArmPerformanceMonitors::ControlGroup(const ArmCoprocAccess& a) {
    const bool user = (user_enable_ & kUserEnable) != 0u;
    switch (a.opc2) {
    case 0:
        if (a.read) {
            return {.action = A::kReadCallback, .user_allowed = user, .context = this,
                    .read = &ReadControl};
        }
        return {.action = A::kWriteCallback, .user_allowed = user, .context = this,
                .write = &WriteControl};
    case 1:
        if (a.read) return {.action = A::kRegister, .user_allowed = user, .reg = &count_enable_};
        return {.action = A::kWriteCallback, .user_allowed = user, .context = this,
                .write = &WriteCountEnableSet};
    case 2:
        if (a.read) return {.action = A::kRegister, .user_allowed = user, .reg = &count_enable_};
        return {.action = A::kWriteCallback, .user_allowed = user, .context = this,
                .write = &WriteCountEnableClear};
    case 3:
        if (a.read) {
            return {.action = A::kReadCallback, .user_allowed = user, .context = this,
                    .read = &ReadOverflow};
        }
        return {.action = A::kWriteCallback, .user_allowed = user, .context = this,
                .write = &WriteOverflow};
    case 4:
        if (a.read) return {.action = A::kUnimplemented};
        return {.action = A::kWriteCallback, .user_allowed = user, .context = this,
                .write = &WriteSoftwareIncrement};
    case 5:
        if (a.read) return {.action = A::kRegister, .user_allowed = user, .reg = &selection_};
        return {.action = A::kWriteCallback, .user_allowed = user, .context = this,
                .write = &WriteSelection};
    default:
        return {.action = A::kUndefined};
    }
}

ArmCoprocDecision ArmPerformanceMonitors::CounterGroup(const ArmCoprocAccess& a) {
    const bool user = (user_enable_ & kUserEnable) != 0u;
    switch (a.opc2) {
    case 0:
        if (a.read) {
            return {.action = A::kReadCallback, .user_allowed = user, .context = this,
                    .read = &ReadCycleCount};
        }
        return {.action = A::kWriteCallback, .user_allowed = user, .context = this,
                .write = &WriteCycleCount};
    case 1:
        if (a.read) {
            return {.action = A::kReadCallback, .user_allowed = user, .context = this,
                    .read = &ReadEventType};
        }
        return {.action = A::kWriteCallback, .user_allowed = user, .context = this,
                .write = &WriteEventType};
    case 2:
        if (a.read) {
            return {.action = A::kReadCallback, .user_allowed = user, .context = this,
                    .read = &ReadEventCount};
        }
        return {.action = A::kWriteCallback, .user_allowed = user, .context = this,
                .write = &WriteEventCount};
    default:
        return {.action = A::kUndefined};
    }
}

ArmCoprocDecision ArmPerformanceMonitors::InterruptGroup(const ArmCoprocAccess& a) {
    switch (a.opc2) {
    case 0:
        if (a.read) return {.action = A::kRegister, .user_allowed = true, .reg = &user_enable_};
        return {.action = A::kWriteCallback, .context = this, .write = &WriteUserEnable};
    case 1:
        if (a.read) return {.action = A::kRegister, .reg = &interrupt_enable_};
        return {.action = A::kWriteCallback, .context = this, .write = &WriteInterruptEnableSet};
    case 2:
        if (a.read) return {.action = A::kRegister, .reg = &interrupt_enable_};
        return {.action = A::kWriteCallback, .context = this, .write = &WriteInterruptEnableClear};
    default:
        return {.action = A::kUndefined};
    }
}

uint32_t ArmPerformanceMonitors::SelectedCounter() const {
    const uint32_t counter = selection_ & kSelectionMask;
    if (counter >= event_counters_) {
        emu_.Get<Fatal>().Die("ARM PMU: PMSELR selects counter %u, the core implements %u",
                              counter, event_counters_);
    }
    return counter;
}

bool ArmPerformanceMonitors::Counting(uint32_t counter) const {
    return (control_ & kEnable) != 0u && (count_enable_ & (1u << counter)) != 0u;
}

bool ArmPerformanceMonitors::CountsCycles(uint32_t counter) const {
    return Counting(counter) && event_type_[counter] == kCpuCyclesEvent;
}

uint64_t ArmPerformanceMonitors::CycleCounterSource(uint64_t cycles) const {
    return (control_ & kCycleDivider) != 0u ? cycles / kCycleDividerTicks : cycles;
}

uint32_t ArmPerformanceMonitors::ImplementedCounters() const {
    return ((1u << event_counters_) - 1u) | kCycleCounterBit;
}

void ArmPerformanceMonitors::Accumulate(uint32_t& count, uint64_t elapsed, uint32_t counter) {
    const uint64_t total = static_cast<uint64_t>(count) + elapsed;
    if (total >= kCounterWrap) overflow_ |= 1u << counter;
    count = static_cast<uint32_t>(total);
}

void ArmPerformanceMonitors::Sync() {
    const uint64_t now    = clock_->Cycles();
    const uint64_t source = CycleCounterSource(now);
    if (Counting(kCycleCounter)) Accumulate(cycle_count_, source - cycle_base_, kCycleCounter);
    cycle_base_ = source;
    for (uint32_t i = 0; i < event_counters_; ++i) {
        if (CountsCycles(i)) Accumulate(event_count_[i], now - event_base_[i], i);
        event_base_[i] = now;
    }
}

void ArmPerformanceMonitors::Rebase() {
    const uint64_t now = clock_->Cycles();
    cycle_base_        = CycleCounterSource(now);
    for (uint32_t i = 0; i < event_counters_; ++i) event_base_[i] = now;
}

void ArmPerformanceMonitors::Schedule() {
    uint64_t due = kNeverDue;
    if (Counting(kCycleCounter)) {
        const uint64_t wrap = cycle_base_ + (kCounterWrap - cycle_count_);
        due = (control_ & kCycleDivider) != 0u ? wrap * kCycleDividerTicks : wrap;
    }
    for (uint32_t i = 0; i < event_counters_; ++i) {
        if (CountsCycles(i)) due = std::min(due, event_base_[i] + (kCounterWrap - event_count_[i]));
    }
    if (due == kNeverDue) {
        clock_->Disarm(overflow_event_);
    } else {
        clock_->Arm(overflow_event_, due);
    }
}

void ArmPerformanceMonitors::DriveIrq(bool force) {
    const bool level = (control_ & kEnable) != 0u && (overflow_ & interrupt_enable_) != 0u;
    if (!force && level == irq_level_) return;
    irq_level_ = level;
    IrqController& intc = emu_.Get<IrqController>();
    if (level) {
        intc.AssertIrq(static_cast<int>(interrupt_line_));
    } else {
        intc.DeAssertIrq(static_cast<int>(interrupt_line_));
    }
}

void ArmPerformanceMonitors::Reset() {
    clock_->Disarm(overflow_event_);
    control_          = 0u;
    count_enable_     = 0u;
    interrupt_enable_ = 0u;
    overflow_         = 0u;
    selection_        = 0u;
    user_enable_      = 0u;
    cycle_count_      = 0u;
    irq_level_        = false;
    event_type_.fill(0u);
    event_count_.fill(0u);
    Rebase();
}

uint32_t CERFENGINE_FASTCALL ArmPerformanceMonitors::ReadControl(void* self) {
    const ArmPerformanceMonitors& pmu = Self(self);
    return pmu.identification_ | pmu.control_;
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteControl(void* self, uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.Sync();
    if ((value & kCycleCounterReset) != 0u) pmu.cycle_count_ = 0u;
    if ((value & kEventCounterReset) != 0u) {
        std::fill_n(pmu.event_count_.begin(), pmu.event_counters_, 0u);
    }
    pmu.control_ = value & kControlWritable;
    pmu.Rebase();
    pmu.Schedule();
    pmu.DriveIrq(false);
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteCountEnableSet(void* self, uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.Sync();
    pmu.count_enable_ |= value & pmu.ImplementedCounters();
    pmu.Rebase();
    pmu.Schedule();
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteCountEnableClear(void* self, uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.Sync();
    pmu.count_enable_ &= ~value;
    pmu.Rebase();
    pmu.Schedule();
}

uint32_t CERFENGINE_FASTCALL ArmPerformanceMonitors::ReadOverflow(void* self) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.Sync();
    pmu.Schedule();
    pmu.DriveIrq(false);
    return pmu.overflow_;
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteOverflow(void* self, uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.Sync();
    pmu.overflow_ &= ~value;
    pmu.Schedule();
    pmu.DriveIrq(false);
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteSoftwareIncrement(void* self,
                                                                      uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.Sync();
    for (uint32_t i = 0; i < pmu.event_counters_; ++i) {
        if ((value & (1u << i)) != 0u && pmu.Counting(i) &&
            pmu.event_type_[i] == kSoftwareIncrementEvent) {
            pmu.Accumulate(pmu.event_count_[i], 1u, i);
        }
    }
    pmu.Schedule();
    pmu.DriveIrq(false);
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteSelection(void* self, uint32_t value) {
    Self(self).selection_ = value & kSelectionMask;
}

uint32_t CERFENGINE_FASTCALL ArmPerformanceMonitors::ReadCycleCount(void* self) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.Sync();
    pmu.Schedule();
    pmu.DriveIrq(false);
    return pmu.cycle_count_;
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteCycleCount(void* self, uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.Sync();
    pmu.cycle_count_ = value;
    pmu.Rebase();
    pmu.Schedule();
    pmu.DriveIrq(false);
}

uint32_t CERFENGINE_FASTCALL ArmPerformanceMonitors::ReadEventType(void* self) {
    const ArmPerformanceMonitors& pmu = Self(self);
    return pmu.event_type_[pmu.SelectedCounter()];
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteEventType(void* self, uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    const uint32_t counter = pmu.SelectedCounter();
    pmu.Sync();
    pmu.event_type_[counter] = value & kEventTypeMask;
    pmu.Rebase();
    pmu.Schedule();
}

uint32_t CERFENGINE_FASTCALL ArmPerformanceMonitors::ReadEventCount(void* self) {
    ArmPerformanceMonitors& pmu = Self(self);
    const uint32_t counter = pmu.SelectedCounter();
    pmu.Sync();
    pmu.Schedule();
    pmu.DriveIrq(false);
    return pmu.event_count_[counter];
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteEventCount(void* self, uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    const uint32_t counter = pmu.SelectedCounter();
    pmu.Sync();
    pmu.event_count_[counter] = value;
    pmu.Rebase();
    pmu.Schedule();
    pmu.DriveIrq(false);
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteUserEnable(void* self, uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    if ((value & ~kUserEnable) != 0u) {
        pmu.emu_.Get<Fatal>().Die("ARM PMU: PMUSERENR write 0x%08X sets reserved bits", value);
    }
    if (value == pmu.user_enable_) return;
    pmu.user_enable_ = value;
    pmu.emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>().PendTranslationFlush();
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteInterruptEnableSet(void* self,
                                                                       uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.interrupt_enable_ |= value & pmu.ImplementedCounters();
    pmu.DriveIrq(false);
}

void CERFENGINE_FASTCALL ArmPerformanceMonitors::WriteInterruptEnableClear(void* self,
                                                                         uint32_t value) {
    ArmPerformanceMonitors& pmu = Self(self);
    pmu.interrupt_enable_ &= ~value;
    pmu.DriveIrq(false);
}

void ArmPerformanceMonitors::SaveState(StateWriter& w) {
    Sync();
    w.Write<uint32_t>("pmu_control", control_);
    w.Write<uint32_t>("pmu_count_enable", count_enable_);
    w.Write<uint32_t>("pmu_interrupt_enable", interrupt_enable_);
    w.Write<uint32_t>("pmu_overflow", overflow_);
    w.Write<uint32_t>("pmu_selection", selection_);
    w.Write<uint32_t>("pmu_user_enable", user_enable_);
    w.Write<uint32_t>("pmu_cycle_count", cycle_count_);
    w.Write("pmu_event_type", event_type_);
    w.Write("pmu_event_count", event_count_);
}

void ArmPerformanceMonitors::RestoreState(StateReader& r) {
    r.Read("pmu_control", control_);
    r.Read("pmu_count_enable", count_enable_);
    r.Read("pmu_interrupt_enable", interrupt_enable_);
    r.Read("pmu_overflow", overflow_);
    r.Read("pmu_selection", selection_);
    r.Read("pmu_user_enable", user_enable_);
    r.Read("pmu_cycle_count", cycle_count_);
    r.Read("pmu_event_type", event_type_);
    r.Read("pmu_event_count", event_count_);
}

}
