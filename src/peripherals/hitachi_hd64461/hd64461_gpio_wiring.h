#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

class Hd64461Gpio;

class Hd64461GpioWiring : public Service {
public:
    using Service::Service;

    virtual void    TrackDrive(const Hd64461Gpio& gpio) = 0;
    virtual uint8_t NetLevels(uint32_t port) const       = 0;
    virtual void    SaveState(StateWriter& w)           = 0;
    virtual void    RestoreState(StateReader& r)        = 0;
};

}
