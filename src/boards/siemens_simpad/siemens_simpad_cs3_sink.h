#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class SiemensSimpadCs3Sink : public Service {
public:
    using Service::Service;
    ~SiemensSimpadCs3Sink() override = default;

    virtual void OnCs3LatchChanged(uint16_t latch) = 0;

    virtual bool CardVs1Grounded() const = 0;
    virtual bool CardVs2Grounded() const = 0;
};

}
