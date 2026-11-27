#include "pm8058_power_key.h"

#include "../../core/board_parts.h"
#include "../../core/deferred_jobs.h"
#include "pm8058_power.h"
#include "qualcomm_pm8058_part_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr int64_t kNsPerMs = 1000000;

}

REGISTER_SERVICE(Pm8058PowerKey);

bool Pm8058PowerKey::ShouldRegister() {
    return emu_.Get<BoardParts>().Has(PartId::QualcommPm8058);
}

void Pm8058PowerKey::OnReady() {
    power_ = &emu_.Get<Pm8058Power>();
    clock_ = &emu_.Get<GuestCycleClock>();
    hold_  = clock_->Add([this] { OnHoldExpired(); });
    power_->AddPowerOffListener([this] { clock_->Disarm(hold_); });
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind kind) {
        if (kind != ResetLineKind::Rtc || power_->Off()) return;
        clock_->Disarm(hold_);
        hard_reset_ms_  = 0u;
        hard_reset_off_ = false;
    });
    reset.RegisterResetReleaseListener([this] {
        if (!user_wake_held_) return;
        user_wake_held_ = false;
        ApplyPressed(false);
    });
    emu_.Get<GuestDeepSleep>().RegisterUserWakeInput([this] {
        user_wake_held_ = true;
        ApplyPressed(true);
    });
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] {
        clock_->Disarm(hold_);
        user_wake_held_ = false;
        ApplyPressed(false);
    });
}

void Pm8058PowerKey::SetPressed(bool pressed) {
    emu_.Get<DeferredJobs>().PostToGuestThread([this, pressed] { ApplyPressed(pressed); });
}

void Pm8058PowerKey::ConfigureHardReset(uint32_t hold_ms, bool power_off) {
    hard_reset_ms_  = hold_ms;
    hard_reset_off_ = power_off;
    LOG(Periph, "[PM8058] power key hard reset after %u ms, power off %d\n", hold_ms,
        power_off ? 1 : 0);
}

void Pm8058PowerKey::AddListener(std::function<void(bool)> fn) {
    listeners_.Add(std::move(fn));
}

void Pm8058PowerKey::ApplyPressed(bool pressed) {
    if (pressed == pressed_) return;
    pressed_ = pressed;
    LOG(Periph, "[PM8058] power key %s, pmic off %d\n", pressed ? "press" : "release",
        power_->Off() ? 1 : 0);
    if (power_->Off()) {
        if (pressed) power_->RequestTurnOn();
        return;
    }
    if (!pressed) {
        clock_->Disarm(hold_);
    } else if (hard_reset_ms_ != 0u) {
        clock_->Arm(hold_, clock_->CycleAfterNs(static_cast<int64_t>(hard_reset_ms_) * kNsPerMs));
    }
    listeners_.Call(pressed);
}

void Pm8058PowerKey::OnHoldExpired() {
    LOG(Periph, "[PM8058] power key held %u ms, hard reset, power off %d\n", hard_reset_ms_,
        hard_reset_off_ ? 1 : 0);
    if (hard_reset_off_) {
        power_->PowerOff();
    } else {
        power_->Restart();
    }
}

void Pm8058PowerKey::SaveState(StateWriter& w) {
    w.Write<uint8_t>("pm8058_power_key_pressed", pressed_ ? 1u : 0u);
    w.Write<uint32_t>("pm8058_power_key_reset_ms", hard_reset_ms_);
    w.Write<uint8_t>("pm8058_power_key_reset_off", hard_reset_off_ ? 1u : 0u);
}

void Pm8058PowerKey::RestoreState(StateReader& r) {
    uint8_t pressed = 0u, reset_off = 0u;
    r.Read("pm8058_power_key_pressed", pressed);
    r.Read("pm8058_power_key_reset_ms", hard_reset_ms_);
    r.Read("pm8058_power_key_reset_off", reset_off);
    pressed_        = pressed != 0u;
    hard_reset_off_ = reset_off != 0u;
}

}
