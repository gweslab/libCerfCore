#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/rated_tick_count.h"
#include "../phone_line/phone_line.h"
#include "rcvdl56dpfl_data_pump.h"
#include "rcvdl56dpfl_data_pump_host.h"

#include <cstdint>

namespace cerfengine { class HostRequestChannel; }

namespace cerfcore {

class Rcvdl56dpflDataPumpPeripheral : public Peripheral, private Rcvdl56dpflDataPumpHost {
public:
    using Peripheral::Peripheral;

    void OnShutdown() override;
    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    void    StartDataPump(const Rcvdl56dpflSelfTest& self_test);
    uint8_t ReadA1AddressedRegister(uint32_t addr);
    void    WriteA1AddressedRegister(uint32_t addr, uint8_t value);
    void    SetResetPin(bool high);

    virtual void DriveIrq(bool asserted) = 0;

private:
    void     SetIrqLevel(bool asserted) override;
    void     SetBitRate(uint32_t bits_per_second) override;
    uint64_t BitTickNow() override;
    uint64_t BitTicksElapsed() override;
    void     ArmTimer(uint64_t tick) override;
    int64_t  GuestNs() override;
    [[noreturn]] void Halt(const char* reason) override;
    void     RescaleBitClock();
    uint32_t A1AddressedRegister(uint32_t addr, uint8_t value);

    GuestCycleClock*                clock_    = nullptr;
    GuestCycleClock::Event*         event_    = nullptr;
    cerfengine::HostRequestChannel* requests_ = nullptr;
    RatedTickCount          bit_ticks_{emu_};
    GuestCycleClock::Rate   bit_rate_{};
    bool                    timed_ = false;
    PhoneLine               phone_{emu_, "Modem"};
    Rcvdl56dpflDataPump     pump_{*this, phone_};
};

}
