#include "hp_jornada_540_notification_led.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/machine_object_tree.h>
#include <cerfcore/state/emulation_freeze.h>
#include <cerfcore/state/state_stream.h>
#include "hp_jornada_540_battery.h"
#include "hp_jornada_540_buttons.h"
#include "hp_jornada_540_family_id.h"

#include <algorithm>

namespace cerfcore {

namespace {

constexpr int64_t   kBlinkPeriodNs     = 1500000000;
constexpr uint32_t  kBlinkHalfMs       = 750u;
constexpr uint32_t  kDisplayOffFlashMs = 200u;
constexpr uint32_t  kDisplayOffDarkMs  = 2800u;
constexpr ObjectRgb kGreen{60, 230, 90};
constexpr ObjectRgb kAmber{255, 176, 0};
constexpr ObjectRgb kRed{235, 40, 30};

}

REGISTER_SERVICE(HpJornada540NotificationLed);

bool HpJornada540NotificationLed::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada540;
}

void HpJornada540NotificationLed::OnReady() {
    battery_   = &emu_.Get<HpJornada540Battery>();
    battery_->AddChangeListener([this] { Refresh(); });
    clock_     = &emu_.Get<GuestCycleClock>();
    end_event_ = clock_->Add([this] { End(); });
    clock_->RegisterRateListener([this] {
        std::lock_guard<std::mutex> lk(mtx_);
        if (blinking_) ArmEndLocked();
    });
    buttons_ = &emu_.Get<HpJornada540Buttons>();
    auto& tree = emu_.Get<MachineObjectTree>();
    tree.Add(this);
    tree.Add(&button_, this);
}

void HpJornada540NotificationLed::DriveButton(bool pressed) {
    auto frozen = emu_.Get<EmulationFreeze>().WorkerSection();
    buttons_->SetPressed(HpJornada540Button::kNotification, pressed);
}

void HpJornada540NotificationLed::Start() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        blinking_ = true;
        end_ns_   = clock_->NowNs() + kBlinkPeriodNs * count_;
        ArmEndLocked();
    }
    Refresh();
}

void HpJornada540NotificationLed::Stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        blinking_ = false;
        clock_->Disarm(end_event_);
    }
    Refresh();
}

void HpJornada540NotificationLed::WriteBlinkCount(uint16_t count) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (blinking_) {
        end_ns_ += kBlinkPeriodNs * (static_cast<int64_t>(count) - static_cast<int64_t>(count_));
        ArmEndLocked();
    }
    count_ = count;
}

uint16_t HpJornada540NotificationLed::BlinkCount() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return count_;
}

bool HpJornada540NotificationLed::Blinking() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return blinking_;
}

void HpJornada540NotificationLed::SetDisplayOff(bool off) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (display_off_ == off) return;
        display_off_ = off;
    }
    Refresh();
}

void HpJornada540NotificationLed::ArmEndLocked() {
    clock_->Arm(end_event_, clock_->CycleAtNs(end_ns_));
}

void HpJornada540NotificationLed::End() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!blinking_ || clock_->NowNs() < end_ns_) return;
        blinking_ = false;
    }
    Refresh();
}

void HpJornada540NotificationLed::SaveState(StateWriter& w) const {
    std::lock_guard<std::mutex> lk(mtx_);
    const int64_t left = blinking_ ? std::max<int64_t>(end_ns_ - clock_->NowNs(), 0) : 0;
    w.Write<uint8_t>("blinking", blinking_ ? 1u : 0u);
    w.Write("blink_count", count_);
    w.Write("blink_left_ns", static_cast<uint64_t>(left));
}

void HpJornada540NotificationLed::RestoreState(StateReader& r) {
    uint8_t  blinking = 0u;
    uint64_t left     = 0u;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        r.Read("blinking", blinking);
        r.Read("blink_count", count_);
        r.Read("blink_left_ns", left);
        blinking_ = blinking != 0u;
        end_ns_   = clock_->NowNs() + static_cast<int64_t>(left);
        if (blinking_) {
            ArmEndLocked();
        } else {
            clock_->Disarm(end_event_);
        }
    }
    Refresh();
}

std::optional<ObjectRgb> HpJornada540NotificationLed::ChargeColor() const {
    if (battery_->Charging()) return kAmber;
    if (battery_->OnExternalPower()) return kGreen;
    return std::nullopt;
}

LampState HpJornada540NotificationLed::Lamp() const {
    bool blinking    = false;
    bool display_off = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        blinking    = blinking_;
        display_off = display_off_;
    }
    const std::optional<ObjectRgb> charge = ChargeColor();
    if (display_off) {
        const std::optional<ObjectRgb> between = blinking ? std::optional<ObjectRgb>(kGreen) : charge;
        return LampState{true, true, kRed, kDisplayOffFlashMs, kDisplayOffDarkMs, between};
    }
    if (blinking) return LampState{true, true, kGreen, kBlinkHalfMs, kBlinkHalfMs, charge};
    return charge ? LampState{true, false, *charge} : LampState{};
}

}
