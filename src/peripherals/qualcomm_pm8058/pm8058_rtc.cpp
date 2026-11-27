#include "pm8058_rtc.h"

#include "pm8058_irq.h"
#include "pm8058_power.h"

#include "../../core/board_parts.h"
#include "qualcomm_pm8058_part_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <iterator>

namespace cerfcore {

namespace {

/* Linux drivers/rtc/rtc-pm8xxx.c: PM8xxx_RTC_ENABLE BIT(7),
   PM8xxx_RTC_ALARM_CLEAR BIT(0), and pm8058_regs.alarm_en BIT(1). */
constexpr uint8_t kEnable      = 1u << 7;
constexpr uint8_t kAlarmEnable = 1u << 1;
constexpr uint8_t kAlarmClear  = 1u << 0;
constexpr uint8_t kCtrlBits    = kEnable | kAlarmEnable;

constexpr uint64_t kCountHz   = 1u;
constexpr uint64_t kCountWrap = 1ull << 32;

constexpr uint32_t kAlarmIrq = 39u;
constexpr uint32_t kRtcIrq   = 53u;

}  // namespace

bool Pm8058Rtc::ShouldRegister() {
    return emu_.Get<BoardParts>().Has(PartId::QualcommPm8058);
}

void Pm8058Rtc::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    irq_   = &emu_.Get<Pm8058Irq>();
    power_ = &emu_.Get<Pm8058Power>();
    irq_->GuardLineShape(kAlarmIrq);
    irq_->GuardUnmodeledSource(kRtcIrq);
    ticks_.Attach(kCountHz, 1u);
    alarm_event_ = clock_->Add([this] { OnAlarmMatch(); });
    PowerOn();
    clock_->RegisterRateListener([this] { OnRateChange(); });
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind kind) {
        if (kind == ResetLineKind::Rtc && !power_->Off()) PowerOn();
    });
    reset.RegisterResetReleaseListener([this] { OnResetRelease(); });
    auto& sleep = emu_.Get<GuestDeepSleep>();
    sleep.RegisterParkWakeSource([this] { return AlarmTurnOnDue(); });
    sleep.RegisterParkWakeDue([this] { return AlarmTurnOnSleptNs(); });
}

void Pm8058Rtc::PowerOn() {
    std::lock_guard<std::mutex> lk(mtx_);
    ctrl_         = kEnable;
    alarm_status_ = false;
    std::fill(std::begin(alarm_), std::end(alarm_), uint8_t{0});
    ticks_.SetCounting(true);
    ticks_.Rebase();
    offset_     = 0u;
    armed_tick_ = 0u;
    clock_->Disarm(alarm_event_);
}

bool Pm8058Rtc::AlarmTurnOnDue() {
    std::lock_guard<std::mutex> lk(mtx_);
    return power_->Off() && AlarmArmedLocked() && ticks_.Now() >= armed_tick_;
}

int64_t Pm8058Rtc::AlarmTurnOnSleptNs() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!power_->Off() || !AlarmArmedLocked()) return GuestDeepSleep::kNoParkWake;
    return ticks_.SleptNsAtTick(armed_tick_);
}

bool Pm8058Rtc::RunningLocked() const { return (ctrl_ & kEnable) != 0u; }

bool Pm8058Rtc::AlarmArmedLocked() const {
    return RunningLocked() && (ctrl_ & kAlarmEnable) != 0u;
}

uint32_t Pm8058Rtc::CountLocked() {
    return static_cast<uint32_t>(ticks_.Now()) + offset_;
}

uint32_t Pm8058Rtc::AlarmLocked() const { return cerf::le::U32(alarm_); }

void Pm8058Rtc::SetCountLocked(uint32_t count) {
    offset_ = count - static_cast<uint32_t>(ticks_.Now());
}

uint64_t Pm8058Rtc::MatchTickLocked() {
    const uint64_t now   = ticks_.Now();
    const uint32_t delta = AlarmLocked() - (static_cast<uint32_t>(now) + offset_);
    return now + (delta == 0u ? kCountWrap : delta);
}

void Pm8058Rtc::SetAlarmStatusLocked(bool high) {
    alarm_status_ = high;
    irq_->SetSourceLevel(kAlarmIrq, high);
}

void Pm8058Rtc::ArmAlarmLocked() {
    if (!AlarmArmedLocked()) {
        clock_->Disarm(alarm_event_);
        return;
    }
    armed_tick_ = MatchTickLocked();
    ticks_.ArmAt(alarm_event_, armed_tick_);
}

void Pm8058Rtc::ReArmLocked() {
    if (!AlarmArmedLocked()) {
        clock_->Disarm(alarm_event_);
        return;
    }
    ticks_.ArmAt(alarm_event_, armed_tick_);
}

void Pm8058Rtc::RequireComparatorUnequalLocked(const char* write) {
    if ((ctrl_ & kAlarmEnable) != 0u && CountLocked() == AlarmLocked()) {
        emu_.Get<Fatal>().Die(
            "pm8058 rtc: %s leaves count 0x%08X equal to alarm 0x%08X under "
            "control 0x%02X", write, CountLocked(), AlarmLocked(), ctrl_);
    }
}

void Pm8058Rtc::OnAlarmMatch() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (alarm_status_) {
        emu_.Get<Fatal>().Die(
            "pm8058 rtc: alarm 0x%08X matched with the alarm status still set",
            AlarmLocked());
    }
    SetAlarmStatusLocked(true);
    ArmAlarmLocked();
}

void Pm8058Rtc::OnResetRelease() {
    std::lock_guard<std::mutex> lk(mtx_);
    ReArmLocked();
    irq_->SetSourceLevel(kAlarmIrq, alarm_status_);
}

void Pm8058Rtc::OnRateChange() {
    std::lock_guard<std::mutex> lk(mtx_);
    ticks_.Rescale();
    ReArmLocked();
}

uint8_t Pm8058Rtc::ReadReg(uint16_t reg) {
    std::lock_guard<std::mutex> lk(mtx_);

    if (reg == kRegCtrl)      return ctrl_;
    if (reg == kRegAlarmCtl2) return alarm_status_ ? kAlarmClear : 0u;

    if (reg >= kRegWrite && reg < kRegWrite + kBytes) return 0u;
    if (reg >= kRegRead && reg < kRegRead + kBytes) {
        const uint32_t count = CountLocked();
        return (uint8_t)(count >> (8u * (uint32_t)(reg - kRegRead)));
    }
    if (reg >= kRegAlarmRw && reg < kRegAlarmRw + kBytes) {
        return alarm_[reg - kRegAlarmRw];
    }

    emu_.Get<Fatal>().Die(
        "pm8058 rtc: register 0x%03X is not one of the block's fourteen", reg);
}

void Pm8058Rtc::WriteReg(uint16_t reg, uint8_t value) {
    std::lock_guard<std::mutex> lk(mtx_);

    if (reg == kRegCtrl) {
        if ((value & (uint8_t)~kCtrlBits) != 0u) {
            emu_.Get<Fatal>().Die(
                "pm8058 rtc: control write 0x%02X sets bits 0x%02X", value,
                (unsigned)(value & (uint8_t)~kCtrlBits));
        }
        ticks_.SetCounting((value & kEnable) != 0u);
        ctrl_ = value;
        RequireComparatorUnequalLocked("control write");
        ArmAlarmLocked();
        return;
    }

    if (reg >= kRegWrite && reg < kRegWrite + kBytes) {
        const uint32_t shift = 8u * (uint32_t)(reg - kRegWrite);
        const uint32_t count = CountLocked();
        SetCountLocked((count & ~(0xFFu << shift)) | ((uint32_t)value << shift));
        RequireComparatorUnequalLocked("counter write");
        ArmAlarmLocked();
        return;
    }

    if (reg == kRegAlarmCtl2) {
        if (value != 0u) {
            emu_.Get<Fatal>().Die(
                "pm8058 rtc: alarm control 2 write 0x%02X", value);
        }
        RequireComparatorUnequalLocked("alarm status clear");
        SetAlarmStatusLocked(false);
        return;
    }

    if (reg >= kRegAlarmRw && reg < kRegAlarmRw + kBytes) {
        alarm_[reg - kRegAlarmRw] = value;
        RequireComparatorUnequalLocked("alarm write");
        ArmAlarmLocked();
        return;
    }

    emu_.Get<Fatal>().Die(
        "pm8058 rtc: write of 0x%02X to register 0x%03X", value, reg);
}

void Pm8058Rtc::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    ticks_.Save(w, "rtc_");
    w.Write<uint32_t>("offset", offset_);
    w.Write<uint64_t>("armed_tick", armed_tick_);
    w.Write<uint8_t>("ctrl", ctrl_);
    w.Write<uint8_t>("alarm_status", alarm_status_ ? 1u : 0u);
    for (uint32_t i = 0; i < kBytes; ++i) {
        w.Write<uint8_t>("alarm", alarm_[i]);
    }
}

void Pm8058Rtc::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    uint32_t offset = 0;
    uint64_t armed  = 0;
    uint8_t  ctrl = 0, status = 0;
    ticks_.Restore(r, "rtc_");
    r.Read("offset", offset);
    r.Read("armed_tick", armed);
    r.Read("ctrl", ctrl);
    r.Read("alarm_status", status);
    for (uint32_t i = 0; i < kBytes; ++i) {
        r.Read("alarm", alarm_[i]);
    }
    offset_       = offset;
    armed_tick_   = armed;
    ctrl_         = ctrl;
    alarm_status_ = status != 0u;
    ReArmLocked();
}

REGISTER_SERVICE(Pm8058Rtc);

}
