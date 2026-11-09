#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <string>

namespace cerfcore {

class NecVrc4171aBoard : public Service {
public:
    using Service::Service;

    virtual uint32_t IsaIoBase() const = 0;
    virtual uint32_t IsaMemBase() const = 0;
    virtual uint16_t SlotBConfiguration() const = 0;
    virtual std::string SlotALabel() const = 0;
    virtual bool     SensePullupsOnCardVcc(int bank) const = 0;
    virtual void     SetIrqLevel(bool active) = 0;
    virtual uint8_t  GpioInputLevels() const = 0;
    virtual bool     GpioPinsWired() const = 0;
};

}
