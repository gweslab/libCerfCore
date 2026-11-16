#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Imx6DisplayClockInput : public Service {
public:
    using Service::Service;

    virtual uint64_t PixelClockHz() const = 0;
};

}
