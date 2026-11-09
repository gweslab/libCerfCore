#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"

#include <cstdint>
#include <mutex>

namespace cerfcore {

class NecVrc4171a;
class NecVrc4171aLcd;
class StateReader;
class StateWriter;
class Vr41xxGiu;

class CompaqAero1500Battery : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint16_t CellVoltageWord();
    uint16_t ChargeCurrentWord();

private:
    bool     Charging();
    bool     UnderLoad();
    uint16_t TargetVoltageWord();
    void     DrivePins();
    void     SaveState(StateWriter& w);
    void     RestoreState(StateReader& r);

    Vr41xxGiu*      giu_     = nullptr;
    NecVrc4171aLcd* lcd_     = nullptr;
    NecVrc4171a*    vrc4171_ = nullptr;
    BatteryWidget   battery_;

    std::mutex mtx_;
    uint16_t   voltage_word_ = 0;
};

}
