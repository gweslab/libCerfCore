#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class MicrelKsz9021Wiring : public Service {
public:
    using Service::Service;

    virtual uint32_t PhyAddress() const = 0;
};

}
