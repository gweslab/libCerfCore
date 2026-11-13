#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Hd64461TimerOutput : public Service {
public:
    using Service::Service;

    virtual void Pulse(uint32_t channel, uint32_t rate_hz) = 0;
};

}
