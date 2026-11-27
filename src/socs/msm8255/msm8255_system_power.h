#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Msm8255SystemPower : public Service {
public:
    using Service::Service;

    virtual void PowerDown() = 0;
    virtual void ConfigurePowerKeyReset(uint32_t hold_ms, bool power_off) = 0;
};

}
