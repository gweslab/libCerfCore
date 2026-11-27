#pragma once

#include "msm8255_mdp_mixer.h"
#include "msm8255_mdp_pixel.h"

#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

class Msm8255MdpOutput : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    uint32_t PresentOverlay0(const Msm8255MdpRegisterRead& reg);
    uint32_t PresentDmaS(const Msm8255MdpRegisterRead& reg);

private:
    void WriteBack(const Msm8255MdpRegisterRead& reg, const Msm8255MdpMixer::Frame& frame,
                   uint32_t blt_format);
    uint32_t Deliver(const Msm8255MdpRegisterRead& reg, uint32_t dma, uint32_t color,
                     uint32_t ld_param, const Msm8255MdpPixel* pixels, uint32_t width,
                     uint32_t height);

    std::vector<Msm8255MdpPixel> dma_s_frame_;
    std::vector<uint8_t> row_;
    std::vector<uint8_t> line_;
};

}
