#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "../../execution/guest_cycle_clock.h"
#include "../rated_tick_count.h"

#include <cstdint>

namespace cerfcore {

class S3C2410Clocks;

class S3C2410Watchdog : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x53000000u; }
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void WriteRegister(uint32_t off, uint32_t value);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    bool                  Running() const;
    GuestCycleClock::Rate CoreRate() const;
    GuestCycleClock::Rate TickRate(uint32_t wtcon) const;
    uint32_t              CountAt(uint64_t now) const;
    void                  Anchor(uint64_t now, uint32_t count);
    void                  Restart(uint64_t now, uint32_t count);
    void                  Arm();
    void                  WriteControl(uint32_t value, uint64_t now);
    void                  ResetRegisters();
    void                  OnTimeout();
    void                  OnRateChange();

    GuestCycleClock*        clock_  = nullptr;
    S3C2410Clocks*          clocks_ = nullptr;
    GuestCycleClock::Event* event_  = nullptr;
    NarrowRatedTickCount    ticks_;
    uint64_t                anchor_tick_ = 0;
    uint32_t                wtcon_       = 0;
    uint32_t                wtdat_       = 0;
    uint32_t                count_       = 0;
};

}
