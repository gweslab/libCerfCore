#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class KernelImageCopier : public Service {
public:
    using Service::Service;

    void CopyToKernelVa(uint32_t va, const uint8_t* src, uint32_t length,
                        bool volatile_only, const char* image);
    bool IsVolatilePa(uint32_t pa);
};

}
