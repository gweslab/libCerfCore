#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

namespace cerfcore {

class Sh77xxBsc : public Peripheral {
public:
    using Peripheral::Peripheral;

    virtual bool     AreaIsSdram(uint32_t area) const  = 0;
    virtual bool     AreaIsPcmcia(uint32_t area) const = 0;
    virtual uint32_t AreaBusBytes(uint32_t area) const = 0;
};

}
