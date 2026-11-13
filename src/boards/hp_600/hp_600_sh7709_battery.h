#pragma once

#include <cerfcore/core/service.h>

#include "../../host/battery_widget.h"

#include <cstdint>

namespace cerfcore {

class Sh77xxIntc;

class Hp600Sh7709Battery : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool     Charging() const;
    uint16_t MainBatteryAdc() const;

private:
    void DriveAcLine();

    BatteryWidget battery_;
    Sh77xxIntc*   intc_ = nullptr;
};

}
