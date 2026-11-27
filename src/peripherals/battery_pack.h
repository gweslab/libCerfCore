#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>

namespace cerfcore {

class BatteryPack : public Service {
public:
    using Service::Service;

    virtual int      ChargePercent() const      = 0;
    virtual bool     ExternalPower() const      = 0;
    virtual bool     Charging() const           = 0;
    virtual uint32_t VoltageMv() const          = 0;
    virtual int      TemperatureCelsius() const = 0;
    virtual uint32_t DesignCapacityMah() const  = 0;

    virtual void AddChangeListener(std::function<void()> fn) = 0;
};

}
