#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class OalArgsHeader : public Service {
public:
    using Service::Service;

    void Write(uint32_t pa, uint16_t oal_version, uint16_t bsp_version);
};

}
