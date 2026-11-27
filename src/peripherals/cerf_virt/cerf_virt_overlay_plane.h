#pragma once

#include "../../lcd/plane_fetch.h"

#include <cerfcore/core/service.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

class CerfVirtOverlayPlane : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Validate(const CerfVirtOverlay& overlay);
    void Compose(const CerfVirtOverlay& overlay, uint32_t* dib_bgra32, uint32_t dib_w, uint32_t dib_h);

private:
    PlaneSource Source(const CerfVirtOverlay& overlay);
    bool        Backed(uint32_t pa, uint64_t bytes);
    void        Read(uint32_t pa, uint8_t* dst, uint32_t bytes);
    void        Turn(uint32_t clockwise, uint32_t& w, uint32_t& h);

    std::vector<PlaneSample> samples_;
    std::vector<PlaneSample> turned_;
    std::vector<PlaneSample> scaled_;
    std::vector<PlaneSample> columns_;
    PlaneFetch::Scratch      scratch_;
};

}
