#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class ShBoardClock : public Service {
public:
    using Service::Service;

    virtual uint64_t CkioHz() const = 0;
    virtual bool     Pll1Switchable() const = 0;
};

}
