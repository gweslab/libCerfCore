#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"
#include <cerfcore/core/callback_list.h>

#include <cstdint>
#include <functional>

namespace cerfcore {

class Vr41xxGiu;

class IbmWorkpadZ50Battery : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool     OnAc() const;
    int      FillPercent() const;
    bool     Charging() const;
    bool     Discharging() const;
    bool     ChargeComplete() const;
    uint16_t DesignCapacityMah() const;
    uint16_t VoltageMv() const;
    uint16_t FullChargeMv() const;
    int16_t  CurrentMa() const;
    uint16_t Edv1Mv() const;
    uint16_t EdvfMv() const;

    void RegisterChangeListener(std::function<void()> fn);

private:
    bool ChargerDisabled() const;
    void NotifyChange();

    Vr41xxGiu*    giu_ = nullptr;
    BatteryWidget battery_;
    CallbackList<> change_listeners_;
};

}
