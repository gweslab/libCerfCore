#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include "../../execution/guest_cycle_clock.h"
#include "sh77xx_dmac.h"
#include "sh77xx_peripheral_clock_ticks.h"
#include "sh77xx_standby_control.h"

#include <cstdint>

namespace cerfcore {

class Sh77xxCmt : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() override;

    uint32_t MmioBase() const override { return 0x04000070u; }
    uint32_t MmioSize() const override { return 0x08u; }

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    bool     ClockRuns() const;
    uint64_t Ticks() const;
    uint64_t FirstMatch(uint16_t count) const;
    uint16_t Count() const;
    void     Sync();
    void     Anchor(uint16_t count);
    void     Arm();
    void     UpdateCounting();
    void     WriteControl(uint16_t value);
    void     PowerOnReset();

    GuestCycleClock*           clock_  = nullptr;
    GuestCycleClock::Event*    event_  = nullptr;
    Sh77xxDmac*                dmac_   = nullptr;
    Sh77xxPeripheralClockTicks pclk_{emu_};
    uint16_t                   cmstr_        = 0u;
    uint16_t                   cmcsr_        = 0u;
    uint16_t                   cmcor_        = 0xFFFFu;
    uint16_t                   held_count_   = 0u;
    uint16_t                   anchor_count_ = 0u;
    uint64_t                   anchor_tick_  = 0u;
    uint64_t                   next_match_   = 0u;
    uint64_t                   restored_ticks_to_match_ = 0u;
    bool                       counting_     = false;
    bool                       cmf_read_     = false;
};

}
