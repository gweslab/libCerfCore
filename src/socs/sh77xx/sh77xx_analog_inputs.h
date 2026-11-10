#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Sh77xxAnalogInputs : public Service {
public:
    using Service::Service;

    virtual uint16_t Convert(uint32_t channel) = 0;
};

}
