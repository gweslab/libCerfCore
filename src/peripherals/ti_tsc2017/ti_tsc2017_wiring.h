#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class I2cSlave;

class TiTsc2017Wiring : public Service {
public:
    using Service::Service;

    virtual void Attach(I2cSlave* device) = 0;
    virtual uint8_t SlaveAddress() const = 0;
};

}
