#include "guest_sample_clock.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

#include <string>

namespace cerfcore {

void GuestSampleClock::Attach(std::function<void()> on_sample) {
    on_sample_ = std::move(on_sample);
    clock_     = &emu_.Get<GuestCycleClock>();
    event_     = clock_->Add([this] { OnEvent(); });
    clock_->RegisterRateListener([this] { Rescale(); });
}

void GuestSampleClock::Start(GuestCycleClock::Rate rate) {
    rate_ = rate;
    samples_.SetRate(clock_->ClockRate(), rate_);
    samples_.Start(clock_->Cycles());
    next_     = 1u;
    sampling_ = true;
    ArmNext();
}

void GuestSampleClock::SetRate(GuestCycleClock::Rate rate) {
    if (rate == rate_) return;
    rate_ = rate;
    Rescale();
}

void GuestSampleClock::Stop() {
    sampling_ = false;
    clock_->Disarm(event_);
}

void GuestSampleClock::Save(StateWriter& w, const char* prefix) const {
    const std::string p(prefix);
    const uint64_t    now = clock_->Cycles();
    w.Write<uint8_t>((p + "sample_running").c_str(), sampling_ ? 1u : 0u);
    w.Write<uint64_t>((p + "sample_rate_num").c_str(), rate_.num);
    w.Write<uint64_t>((p + "sample_rate_den").c_str(), rate_.den);
    w.Write<uint64_t>((p + "sample_ticks").c_str(), sampling_ ? samples_.TicksAt(now) : 0u);
    (sampling_ ? samples_.TickPhaseAt(now) : CounterPhase{}).Save(w, (p + "sample_tick_phase").c_str());
    w.Write<uint64_t>((p + "sample_next").c_str(), next_);
}

void GuestSampleClock::Restore(StateReader& r, const char* prefix) {
    const std::string p(prefix);
    uint8_t           running = 0;
    uint64_t          num = 0, den = 1, ticks = 0, next = 0;
    CounterPhase      phase;
    r.Read((p + "sample_running").c_str(), running);
    r.Read((p + "sample_rate_num").c_str(), num);
    r.Read((p + "sample_rate_den").c_str(), den);
    r.Read((p + "sample_ticks").c_str(), ticks);
    phase.Restore(r, (p + "sample_tick_phase").c_str());
    r.Read((p + "sample_next").c_str(), next);
    rate_     = GuestCycleClock::Rate{num, den};
    next_     = next;
    sampling_ = running != 0u;
    if (!sampling_) {
        clock_->Disarm(event_);
        return;
    }
    samples_.SetRate(clock_->ClockRate(), rate_);
    samples_.PlaceAtPhase(clock_->Cycles(), ticks, phase);
    ArmNext();
}

void GuestSampleClock::Rescale() {
    if (!sampling_) return;
    samples_.Rescale(clock_->Cycles(), clock_->ClockRate(), rate_);
    ArmNext();
}

void GuestSampleClock::ArmNext() {
    clock_->Arm(event_, samples_.CycleOfTick(next_));
}

void GuestSampleClock::OnEvent() {
    if (!sampling_) return;
    next_ = samples_.TicksAt(clock_->Cycles()) + 1u;
    on_sample_();
    if (sampling_) ArmNext();
}

}
