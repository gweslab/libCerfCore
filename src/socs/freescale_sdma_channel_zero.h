#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class FreescaleSdmaChannelZero : public Service {
public:
    using Service::Service;

    virtual void Run(const uint8_t* bd, uint32_t running_channels, uint32_t sdma_mmio_base) = 0;
    virtual bool ContextWatermark(uint32_t channel, uint32_t& value) const = 0;
    virtual void SaveState(StateWriter& writer) const = 0;
    virtual void RestoreState(StateReader& reader) = 0;
    virtual void Reset() = 0;
};

}
