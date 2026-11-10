#include "sh77xx_peripheral_clock_ticks.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "sh77xx_cpg.h"
#include "sh77xx_frqcr.h"

#include <utility>

namespace cerfcore {

void Sh77xxPeripheralClockTicks::Attach(std::function<void()> on_retime) {
    clock_     = &emu_.Get<GuestCycleClock>();
    frqcr_     = &emu_.Get<Sh77xxFrqcr>();
    cpg_       = &emu_.Get<Sh77xxCpg>();
    on_retime_ = std::move(on_retime);
    rate_      = cpg_->PeripheralRate(frqcr_->Value());
    ticks_.SetRate(clock_->ClockRate(), rate_);
    ticks_.Start(clock_->Cycles());
    clock_->RegisterRateListener([this] { Retime(); });
    frqcr_->RegisterChangeListener([this](uint16_t, uint16_t to) {
        rate_ = cpg_->PeripheralRate(to);
        Retime();
    });
}

uint64_t Sh77xxPeripheralClockTicks::Ticks() const { return ticks_.TicksAt(clock_->Cycles()); }

uint64_t Sh77xxPeripheralClockTicks::CycleOfTick(uint64_t tick) const { return ticks_.CycleOfTick(tick); }

void Sh77xxPeripheralClockTicks::Retime() {
    ticks_.Rescale(clock_->Cycles(), clock_->ClockRate(), rate_);
    on_retime_();
}

void Sh77xxPeripheralClockTicks::Save(StateWriter& w) const {
    const uint64_t now = clock_->Cycles();
    w.Write("pclk_ticks", ticks_.TicksAt(now));
    ticks_.TickPhaseAt(now).Save(w, "pclk_phase");
}

void Sh77xxPeripheralClockTicks::Restore(StateReader& r) {
    r.Read("pclk_ticks", restored_ticks_);
    restored_phase_.Restore(r, "pclk_phase");
}

void Sh77xxPeripheralClockTicks::PostRestore() {
    rate_ = cpg_->PeripheralRate(frqcr_->Value());
    ticks_.SetRate(clock_->ClockRate(), rate_);
    ticks_.PlaceAtPhase(clock_->Cycles(), restored_ticks_, restored_phase_);
}

}
