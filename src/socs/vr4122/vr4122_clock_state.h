#pragma once

#include "../vr41xx/vr41xx_clock_state.h"
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>

namespace cerfcore {

class Vr4122ClockState : public Vr41xxClockState {
public:
    using Vr41xxClockState::Vr41xxClockState;

    bool ShouldRegister() override;
    void OnReady() override;

    uint16_t ClkSpeedReg() const;

    GuestCycleClock::Rate ResetRate() const;
    uint64_t              CyclesPerCountTick() const;
    uint64_t              CyclesPerTclkCounterTick() const;
    uint32_t              ConfigEc() const;

protected:
    const char* RejectedPending(uint16_t value) const override;
    void        ActiveRestored() override;

private:
    uint16_t VtDivMode() const;
    uint16_t TDivMode() const;

    uint16_t clksp_       = 0;
    uint16_t strap_vtdiv_ = 0;
    uint16_t vtdiv_modes_ = 0;
};

}
