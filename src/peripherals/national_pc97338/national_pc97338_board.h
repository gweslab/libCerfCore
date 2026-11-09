#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class NationalPc97338Board : public Service {
public:
    using Service::Service;

    virtual uint32_t IsaIoBase() const = 0;
};

}
