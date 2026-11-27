#pragma once

#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/core/callback_list.h>
#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class BatteryPack;
class StateReader;
class StateWriter;
class TiBq27520RomMode;

class TiBq27520Monitor : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void AddSocIntListener(CallbackList<bool>::Callback listener);
    void Restart();
    void IndicateDataFlashWrite();
    void InsertBattery();
    void RemoveBattery();
    bool BatteryDetected() const;
    bool Soc1() const;
    bool SysDown() const { return sysdown_; }

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    uint8_t  RegistersByte(uint32_t offset) const;
    int32_t  DischargeVoltage(uint32_t offset) const;
    bool     InsertionDetectEnabled() const;
    void     RequireSoc1Disabled() const;
    bool     CrossesSocPoint(int from, int to) const;
    void     Baseline();
    void     OnBatteryChange();
    int64_t  SlotAt(int64_t now_ns) const;
    void     RequestPulse();
    void     ArmSlotPulse();
    void     DeliverSlotPulse();
    void     Pulse();

    BatteryPack*            battery_    = nullptr;
    TiBq27520RomMode*       rom_        = nullptr;
    GuestCycleClock*        clock_      = nullptr;
    GuestCycleClock::Event* slot_event_ = nullptr;
    CallbackList<bool>      soc_int_;

    int     reported_soc_      = 0;
    bool    reported_charging_ = false;
    bool    sysdown_           = false;
    bool    bat_det_           = false;
    int64_t slot_origin_ns_    = 0;
    int64_t pulsed_slot_       = -1;
    bool    slot_pulse_armed_  = false;
};

}
