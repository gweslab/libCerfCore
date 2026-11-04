#pragma once

#include "../serial/serial_line.h"

#include <cstdint>

namespace cerfcore {

class IbmTrackpointHost {
public:
    static constexpr uint64_t kNoEvent = UINT64_MAX;

    virtual SerialLine::LineConfig HostFormat()                                               = 0;
    virtual void                   Receive(uint8_t data, const SerialLine::LineConfig& format) = 0;
    virtual void                   Halt(const char* what, unsigned value)                     = 0;
    virtual uint64_t               LineTickNow()                                              = 0;
    virtual void                   ArmLineTick(uint64_t tick)                                 = 0;

protected:
    ~IbmTrackpointHost() = default;
};

}
