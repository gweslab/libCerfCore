#pragma once

#include "msm8255_mdp_pixel.h"

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

class Msm8255MdpMixer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    struct Frame {
        std::vector<Msm8255MdpPixel> pixels;
        uint32_t width  = 0;
        uint32_t height = 0;
    };

    const Frame& Compose(const Msm8255MdpRegisterRead& reg);

private:
    struct Key {
        bool enabled = false;
        std::array<uint16_t, 3> low{};
        std::array<uint16_t, 3> high{};
    };

    static Key ReadKey(const Msm8255MdpRegisterRead& reg, uint32_t base, bool enabled);
    static bool Matches(const Key& key, const Msm8255MdpPixel& p);
    void Blend(const Msm8255MdpRegisterRead& reg, uint32_t pipe, uint32_t stage);

    Frame frame_;
    std::vector<Msm8255MdpSample> samples_;
};

}
