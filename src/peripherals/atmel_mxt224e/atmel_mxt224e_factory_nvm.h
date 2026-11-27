#pragma once

#include "atmel_mxt224e_map.h"

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class AtmelMxt224eFactoryNvm : public Service {
public:
    using Service::Service;

    virtual const std::array<uint8_t, kMxtNvmBytes>& Bytes() const = 0;
};

}
