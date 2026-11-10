#include "external_device_clock.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/state/state_stream.h>

#include <string>
#include <utility>

namespace cerfcore {

namespace {
constexpr uint64_t kTicksPerSecond = 1'000'000'000u;
}

REGISTER_SERVICE(ExternalDeviceClock);

void ExternalDeviceClock::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    ticks_.Attach(kTicksPerSecond, 1u);
    clock_->RegisterRateListener([this] {
        ticks_.Rescale();
        RearmAll();
    });
    emu_.Get<GuestDeepSleep>().RegisterParkClock([this] { CheckAll(); });
}

int64_t ExternalDeviceClock::NowNs() { return static_cast<int64_t>(ticks_.Now()); }

ExternalDeviceClock::Timer& ExternalDeviceClock::AddTimer(std::function<void()> on_due) {
    timers_.push_back(std::make_unique<Timer>(std::move(on_due)));
    Timer& timer = *timers_.back();
    timer.event_ = clock_->Add([this, &timer] { Check(timer); });
    return timer;
}

void ExternalDeviceClock::Arm(Timer& timer, int64_t delay_ns) {
    SetDeadline(timer, delay_ns);
    ArmEvent(timer);
}

void ExternalDeviceClock::Disarm(Timer& timer) {
    ClearDeadline(timer);
    clock_->Disarm(timer.event_);
}

void ExternalDeviceClock::SetDeadline(Deadline& deadline, int64_t delay_ns) {
    deadline.due_   = ticks_.Now() + static_cast<uint64_t>(delay_ns);
    deadline.armed_ = true;
}

void ExternalDeviceClock::ClearDeadline(Deadline& deadline) { deadline.armed_ = false; }

bool ExternalDeviceClock::Pending(const Deadline& deadline) {
    return deadline.armed_ && ticks_.Now() < deadline.due_;
}

void ExternalDeviceClock::SaveDeadline(const Deadline& deadline, StateWriter& w, const char* prefix) {
    const std::string p(prefix);
    const uint64_t    now  = ticks_.Now();
    const uint64_t    left = deadline.armed_ && deadline.due_ > now ? deadline.due_ - now : 0u;
    w.Write<uint8_t>((p + "armed").c_str(), deadline.armed_ ? 1u : 0u);
    w.Write<uint64_t>((p + "remaining_ns").c_str(), left);
}

void ExternalDeviceClock::RestoreDeadline(Deadline& deadline, StateReader& r, const char* prefix) {
    const std::string p(prefix);
    uint8_t  armed = 0u;
    uint64_t left  = 0u;
    r.Read((p + "armed").c_str(), armed);
    r.Read((p + "remaining_ns").c_str(), left);
    ticks_.Rebase();
    deadline.armed_ = armed != 0u;
    deadline.due_   = ticks_.Now() + left;
}

void ExternalDeviceClock::SaveTimer(Timer& timer, StateWriter& w, const char* prefix) {
    SaveDeadline(timer, w, prefix);
}

void ExternalDeviceClock::RestoreTimer(Timer& timer, StateReader& r, const char* prefix) {
    RestoreDeadline(timer, r, prefix);
    clock_->Disarm(timer.event_);
}

void ExternalDeviceClock::ResumeTimer(Timer& timer) {
    if (timer.armed_) ArmEvent(timer);
}

void ExternalDeviceClock::Check(Timer& timer) {
    if (!timer.armed_) return;
    if (ticks_.Now() < timer.due_) {
        ArmEvent(timer);
        return;
    }
    Disarm(timer);
    timer.on_due_();
}

void ExternalDeviceClock::ArmEvent(Timer& timer) { ticks_.ArmAt(timer.event_, timer.due_); }

void ExternalDeviceClock::CheckAll() {
    for (size_t i = 0; i < timers_.size(); ++i) Check(*timers_[i]);
}

void ExternalDeviceClock::RearmAll() {
    for (size_t i = 0; i < timers_.size(); ++i) {
        if (timers_[i]->armed_) ArmEvent(*timers_[i]);
    }
}

}
