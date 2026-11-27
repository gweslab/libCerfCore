#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>

namespace cerfcore {

struct MipsFixedFields {
    uint32_t mask  = 0;
    uint32_t value = 0;

    bool Accepts(uint32_t written) const { return (written & mask) == value; }
};

class MipsCp0FieldLimits : public Service {
public:
    using Service::Service;

    virtual MipsFixedFields Config() const = 0;
    virtual MipsFixedFields Status() const = 0;
};

}
