#pragma once

#include <cerfcore/core/service.h>
#include "../../peripherals/pcmcia/pcmcia_io_width.h"

#include <cstdint>

namespace cerfcore {

class Sh77xxPcmciaCardBus : public Service {
public:
    using Service::Service;

    virtual uint32_t ReadCycles(uint32_t area, uint32_t offset, bool io, PcmciaIoWidth width, unsigned bytes) = 0;
    virtual void     WriteCycles(uint32_t area, uint32_t offset, bool io, PcmciaIoWidth width, unsigned bytes,
                                 uint32_t value) = 0;
};

}
