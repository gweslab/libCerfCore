#pragma once

#include <cerfcore/peripherals/cerf_virt/cerf_virt_blt_descriptor.h>
#include "cerf_virt_blt_surface.h"
#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace CerfVirt {

class CerfVirtBlitter : public Service, public BltSurfaceAccess {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;

    bool Execute(const CerfBltDescriptor& d);

private:
    bool BlendAAText(const CerfBltDescriptor& d, Surface& dst,
                     const uint32_t d_masks[3], uint32_t d_bpp);
    [[noreturn]] void FatalSurface(const char* what, const CerfBltSurface& s);
    [[noreturn]] void FatalPixel(const char* what, const CerfBltSurface& s,
                                 int32_t x, int32_t y);

    std::vector<int32_t> sx_lut_;
    std::vector<int32_t> sy_lut_;
};

}

}
