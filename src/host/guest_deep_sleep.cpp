#include <cerfcore/host/guest_deep_sleep.h>

#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../core/steady_time.h"
#include "../execution/engine_bridge.h"
#include "../execution/guest_cycle_clock.h"
#include <cerfcore/state/shutdown_action.h>
#include <cerfcore/host/guest_power_notifier.h>
#include <cerfcore/host/notification_stack.h>
#include <cerfcore/host/shutdown_host.h>

#include <chrono>

namespace cerfcore {

REGISTER_SERVICE(GuestDeepSleep);

namespace {
constexpr int kResumeGraceMs = 100;
}

void GuestDeepSleep::OnReady() {
    emu_.Get<GuestCycleClock>().RegisterIdleListener([this] { OnGuestIdle(); });
}

void GuestDeepSleep::RequireNoWakeShape(const char* shape) const {
    if (waker_ == nullptr && clock_stop_ == nullptr && !power_on_wake_) return;
    emu_.Get<Fatal>().Die("GuestDeepSleep: a %s wake registered beside another wake shape", shape);
}

void GuestDeepSleep::RegisterWaker(DeepSleepWaker* waker) {
    RequireNoWakeShape("reset");
    waker_ = waker;
}

void GuestDeepSleep::RegisterClockStopWaker(DeepSleepClockStop* waker) {
    RequireNoWakeShape("clock-stop");
    clock_stop_ = waker;
}

void GuestDeepSleep::RegisterPowerOnWake() {
    RequireNoWakeShape("power-on");
    power_on_wake_ = true;
}

void GuestDeepSleep::RegisterPowerUpListener(std::function<void()> fn) {
    power_up_listeners_.Add(std::move(fn));
}

void GuestDeepSleep::RegisterUserWakeInput(std::function<void()> fn) {
    user_wake_inputs_.Add(std::move(fn));
}

void GuestDeepSleep::RegisterSleepEntryListener(std::function<void()> fn) {
    sleep_entry_listeners_.Add(std::move(fn));
}

void GuestDeepSleep::RegisterParkClock(std::function<void()> fn) {
    park_clocks_.Add(std::move(fn));
}

void GuestDeepSleep::RegisterParkWakeSource(std::function<bool()> fn) {
    park_wake_sources_.push_back(std::move(fn));
}

void GuestDeepSleep::RegisterParkWakeDue(std::function<int64_t()> fn) {
    park_wake_dues_.push_back(std::move(fn));
}

int64_t GuestDeepSleep::CollectParkWakeDue() const {
    int64_t due = kNoParkWake;
    for (auto& fn : park_wake_dues_) {
        const int64_t d = fn();
        if (d < due) due = d;
    }
    return due;
}

void GuestDeepSleep::RefreshParkWakeDue() {
    {
        std::lock_guard<std::mutex> lk(span_mtx_);
        if (sleep_entry_ns_ == 0) return;
    }
    const int64_t due = CollectParkWakeDue();
    std::lock_guard<std::mutex> lk(span_mtx_);
    if (sleep_entry_ns_ == 0) return;
    park_due_ns_ = due;
    LOG(SocReset, "[DEEPSLEEP] RefreshParkWakeDue: park wake due at %lld slept ns\n",
        static_cast<long long>(due));
}

void GuestDeepSleep::PollPark() {
    int64_t due  = kNoParkWake;
    int64_t span = 0;
    {
        std::lock_guard<std::mutex> lk(span_mtx_);
        if (sleep_entry_ns_ != 0) {
            due  = park_due_ns_;
            span = slept_ns_ + (HostSteadyNanos() - sleep_entry_ns_);
        }
    }
    park_clocks_.Call();
    bool           have   = false;
    ShutdownChoice choice = ShutdownChoice::Cancel;
    {
        std::lock_guard<std::mutex> lk(resume_mtx_);
        have = TakePendingLocked(choice);
    }
    if (have) {
        ApplyChoiceInPark(choice);
        return;
    }
    for (auto& fn : park_wake_sources_) {
        if (!fn()) continue;
        LOG(SocReset, "[DEEPSLEEP] PollPark: wake-up event from a park wake source\n");
        DeliverWake(ResumeSource::Hardware);
        return;
    }
    if (due != kNoParkWake && span >= due) {
        emu_.Get<Fatal>().Die("GuestDeepSleep: the park reached the timer wake due at %lld slept ns "
                              "and no park wake source fired",
                              static_cast<long long>(due));
    }
}

void GuestDeepSleep::FinishPark() {
    park_clocks_.Call();
    auto& guest = emu_.Get<EngineBridge>().Guest();
    if (!guest.DeepSleep() || !guest.ResetPending()) return;
    ClaimSleepEnd();
    if (clock_stop_) PowerUpClockStop();
}

bool GuestDeepSleep::ClaimSleepEnd() {
    if (wake_claimed_.exchange(true, std::memory_order_acq_rel)) return false;
    ConsumeSleepSpan();
    MarkSleepEnded();
    return true;
}

bool GuestDeepSleep::TakePendingLocked(ShutdownChoice& choice) {
    if (!choice_pending_) return false;
    choice          = pending_choice_;
    choice_pending_ = false;
    return true;
}

void GuestDeepSleep::MarkSleepEnded() {
    {
        std::lock_guard<std::mutex> lk(resume_mtx_);
        sleep_ended_ = true;
    }
    resume_cv_.notify_all();
}

int64_t GuestDeepSleep::SleptNs() const {
    std::lock_guard<std::mutex> lk(span_mtx_);
    if (sleep_entry_ns_ == 0) return slept_ns_;
    const int64_t span = slept_ns_ + (HostSteadyNanos() - sleep_entry_ns_);
    return span < park_due_ns_ ? span : park_due_ns_;
}

int64_t GuestDeepSleep::ParkWaitNs() const {
    constexpr int64_t kPollNs = 20000000;
    std::lock_guard<std::mutex> lk(span_mtx_);
    if (sleep_entry_ns_ == 0 || park_due_ns_ == kNoParkWake) return kPollNs;
    const int64_t left = park_due_ns_ - (slept_ns_ + (HostSteadyNanos() - sleep_entry_ns_));
    if (left <= 0) return 0;
    return left < kPollNs ? left : kPollNs;
}

void GuestDeepSleep::ClearWakeCause() {
    if (waker_) waker_->ClearSleepWakeCause();
}

void GuestDeepSleep::RegisterResumeVectorProvider(SleepResumeVectorProvider* p) {
    resume_vector_provider_ = p;
}

void GuestDeepSleep::Enter() {
    auto& guest = emu_.Get<EngineBridge>().Guest();
    if (guest.ResetPending()) {
        LOG(SocReset, "[DEEPSLEEP] Enter: reset pending, skip (wake/reboot wins)\n");
        return;
    }
    if (guest.DeepSleep()) {
        LOG(SocReset, "[DEEPSLEEP] Enter: already asleep, skip\n");
        return;
    }
    LOG(SocReset, "[DEEPSLEEP] Enter: sleep begin\n");
    {
        std::lock_guard<std::mutex> lk(resume_mtx_);
        off_.store(true, std::memory_order_release);
        sleep_ended_ = false;
    }
    wake_claimed_.store(false, std::memory_order_release);
    emu_.Get<GuestPowerNotifier>().NotifyPowerDown();
    guest.EnterDeepSleep();
    sleep_entry_listeners_.Call();
    const int64_t due    = CollectParkWakeDue();
    int64_t       due_in = 0;
    {
        std::lock_guard<std::mutex> lk(span_mtx_);
        sleep_entry_ns_ = HostSteadyNanos();
        park_due_ns_    = due;
        due_in          = due - slept_ns_;
    }
    if (due != kNoParkWake) {
        LOG(SocReset, "[DEEPSLEEP] Enter: park wake due in %lld ns\n",
            static_cast<long long>(due_in));
    }
    RePrompt();
}

void GuestDeepSleep::RePrompt() {
    {
        std::lock_guard<std::mutex> lk(resume_mtx_);
        if (prompt_open_ || choice_pending_ || !off_.load(std::memory_order_acquire)) return;
        prompt_open_ = true;
    }
    emu_.Get<ShutdownHost>().RunPromptJob([this] { Recover(); });
}

void GuestDeepSleep::ApplyChoiceInPark(ShutdownChoice c) {
    if (c == ShutdownChoice::Cancel) {
        DeliverWake(ResumeSource::User);
        return;
    }
    if (!ClaimSleepEnd()) {
        LOG(SocReset, "[DEEPSLEEP] PollPark: choice=%d dropped, a wake already ended the sleep\n",
            static_cast<int>(c));
        return;
    }
    LOG(SocReset, "[DEEPSLEEP] PollPark: user choice=%d ends the sleep\n", static_cast<int>(c));
    emu_.Get<ShutdownAction>().Perform(c);
}

void GuestDeepSleep::OnGuestIdle() {
    if (!off_.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lk(resume_mtx_);
        off_.store(false, std::memory_order_release);
        if (choice_pending_ && pending_choice_ == ShutdownChoice::Cancel) choice_pending_ = false;
    }
    resume_cv_.notify_all();
    LOG(SocReset, "[DEEPSLEEP] guest idle after a wake: the guest is on\n");
    emu_.Get<ShutdownHost>().RunPromptJob([this] {
        if (!asking_ || off_.load(std::memory_order_acquire)) return;
        emu_.Get<ShutdownHost>().DismissPrompt();
    });
}

void GuestDeepSleep::ObserveAsleep(bool asleep) {
    bool           have   = false;
    ShutdownChoice choice = ShutdownChoice::Cancel;
    {
        std::lock_guard<std::mutex> lk(resume_mtx_);
        asleep_.store(asleep, std::memory_order_release);
        if (!asleep && choice_pending_ && pending_choice_ != ShutdownChoice::Cancel)
            have = TakePendingLocked(choice);
    }
    auto& stack = emu_.Get<NotificationStack>();
    if (asleep)
        stack.Show(NotificationId::GuestSleeping, NotificationKind::Warning,
                   "Guest is in sleep mode");
    else
        stack.Close(NotificationId::GuestSleeping);
    if (!have) return;
    LOG(SocReset, "[DEEPSLEEP] RunLoop: user choice=%d runs on the woken guest\n",
        static_cast<int>(choice));
    emu_.Get<ShutdownAction>().Perform(choice);
}

void GuestDeepSleep::ConsumeSleepSpan() {
    std::lock_guard<std::mutex> lk(span_mtx_);
    if (sleep_entry_ns_ == 0) return;
    const int64_t span = slept_ns_ + (HostSteadyNanos() - sleep_entry_ns_);
    slept_ns_       = span < park_due_ns_ ? span : park_due_ns_;
    sleep_entry_ns_ = 0;
    park_due_ns_    = kNoParkWake;
}

void GuestDeepSleep::DeliverWake(ResumeSource src) {
    if (!ClaimSleepEnd()) return;
    if (src == ResumeSource::User) {
        user_wake_inputs_.Call();
    }
    if (clock_stop_) {
        PowerUpClockStop();
        emu_.Get<GuestPowerNotifier>().NotifyResume(src);
        emu_.Get<EngineBridge>().Guest().ExitDeepSleep();
        return;
    }
    if (power_on_wake_) {
        emu_.Get<GuestPowerNotifier>().NotifyResume(src);
        emu_.Get<GuestColdBoot>().RequestHardReset();
        return;
    }
    waker_->LatchSleepWakeCause();
    if (resume_vector_provider_) resume_vector_provider_->ApplyPendingResume();
    emu_.Get<EngineBridge>().Guest().SetResetPending(true);
    emu_.Get<GuestPowerNotifier>().NotifyResume(src);
}

void GuestDeepSleep::OnFullRestore() {
    ConsumeSleepSpan();
    {
        std::lock_guard<std::mutex> lk(resume_mtx_);
        choice_pending_ = false;
    }
    const bool asleep = emu_.Get<EngineBridge>().Guest().DeepSleep();
    wake_claimed_.store(!asleep, std::memory_order_release);
    if (asleep) DeliverWake(ResumeSource::User);
}

void GuestDeepSleep::Recover() {
    auto&      host  = emu_.Get<ShutdownHost>();
    const auto ended = [this] { return sleep_ended_ || !off_.load(std::memory_order_acquire); };
    {
        std::unique_lock<std::mutex> lk(resume_mtx_);
        resume_cv_.wait_for(lk, std::chrono::milliseconds(kResumeGraceMs), ended);
        if (ended() || host.PromptOpen()) {
            prompt_open_ = false;
            return;
        }
    }
    LOG(SocReset, "[DEEPSLEEP] Recover: prompt open\n");
    asking_                = true;
    const ShutdownChoice c = host.Ask(ShutdownTrigger::DeepSleep);
    asking_                = false;
    LOG(SocReset, "[DEEPSLEEP] Recover: prompt closed, choice=%d (0=Cancel/wake, 5=Dismissed)\n",
        static_cast<int>(c));
    const bool resets = c == ShutdownChoice::SoftReset || c == ShutdownChoice::HardReset;
    bool       pend   = false;
    {
        std::lock_guard<std::mutex> lk(resume_mtx_);
        prompt_open_ = false;
        if (c == ShutdownChoice::Cancel) pend = off_.load(std::memory_order_acquire);
        if (resets) pend = asleep_.load(std::memory_order_acquire);
        if (pend) {
            choice_pending_ = true;
            pending_choice_ = c;
        }
    }
    if (c == ShutdownChoice::Dismissed) {
        RePrompt();
        return;
    }
    if (pend || c == ShutdownChoice::Cancel) return;
    LOG(SocReset, "[DEEPSLEEP] Recover: choice=%d runs now\n", static_cast<int>(c));
    emu_.Get<ShutdownAction>().Perform(c);
}

void GuestDeepSleep::PowerUpClockStop() {
    clock_stop_->OnPowerUp();
    power_up_listeners_.Call();
}

}
