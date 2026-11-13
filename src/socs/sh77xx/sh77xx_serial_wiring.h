#pragma once

#include <cerfcore/core/service.h>
#include "../../peripherals/serial/serial_endpoint_port.h"

#include <cstdint>
#include <functional>
#include <string>

namespace cerfcore {

class Sh77xxSerialWiring : public Service {
public:
    using Service::Service;

    virtual std::string        CradleLabel(uint32_t channel_base) const                                   = 0;
    virtual bool               DriverPowered(uint32_t channel_base) const                                 = 0;
    virtual SerialControlLines Control(uint32_t channel_base) const                                       = 0;
    virtual void               SetModemInputs(uint32_t channel_base, bool cts, bool dsr, bool ri, bool dcd) = 0;
    virtual void               SetControlListener(uint32_t channel_base, std::function<void()> listener)  = 0;
};

}
