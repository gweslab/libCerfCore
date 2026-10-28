#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Vrc4172GpioConfig : public Service {
public:
    using Service::Service;

    virtual int      GiuIntPin() const = 0;
    virtual uint16_t InitialInputLevels() const = 0;
};

}
