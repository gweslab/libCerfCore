#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

class Sh77xxPortData;

class Sh77xxPortWiring : public Service {
public:
    using Service::Service;

    virtual void    TrackDrive(const Sh77xxPortData& ports) = 0;
    virtual uint8_t WiredInputs(uint32_t port) const        = 0;
    virtual uint8_t NetLevels(uint32_t port) const          = 0;
    virtual void    SaveState(StateWriter& w)               = 0;
    virtual void    RestoreState(StateReader& r)            = 0;
};

}
