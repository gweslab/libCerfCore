#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include "../../execution/guest_cycle_clock.h"
#include "../../socs/oscillator_ticks.h"
#include "../../socs/reload_down_counter.h"

#include <cstdint>

namespace cerfcore {

class Hd64461Gpio;
class Hd64461Intc;
class Hd64461SystemControl;
class Hd64461TimerOutput;

class Hd64461Timer : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x10006000u; }
    uint32_t MmioSize() const override { return 0x10u; }

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteByte(uint32_t addr, uint8_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void OnModuleClockChanged();

private:
    static constexpr uint32_t kChannels = 2u;

    struct Channel {
        ReloadDownCounter       counter{ReloadDownCounter::FlagPoint::kZero};
        GuestCycleClock::Event* event        = nullptr;
        GuestCycleClock::Event* output_event = nullptr;
        uint16_t                control      = 0u;
        bool                    masked       = false;
        bool                    clocked      = true;
    };

    uint32_t Index(const Channel& ch) const;
    uint64_t Ticks(const Channel& ch);
    bool     Counting(const Channel& ch) const;
    void     Arm(Channel& ch);
    void     Drive(Channel& ch);
    void     Sync(Channel& ch);
    void     Pulse(Channel& ch);
    uint16_t ReadCount(Channel& ch);
    void     WriteControl(Channel& ch, uint16_t value);
    uint16_t ReadRequests();
    void     WriteRequests(uint16_t value);
    uint16_t ReadMasks() const;
    void     WriteMasks(uint16_t value);
    void     SetClocked(Channel& ch, bool clocked);
    void     PowerOnReset();
    void     Retime();

    GuestCycleClock*      clock_   = nullptr;
    Hd64461Intc*          intc_    = nullptr;
    Hd64461SystemControl* system_  = nullptr;
    Hd64461Gpio*          gpio_    = nullptr;
    Hd64461TimerOutput*   output_  = nullptr;
    uint64_t              ckio_hz_ = 0u;
    OscillatorTicks       ckio_{emu_, false};
    Channel               ch_[kChannels];
};

}
