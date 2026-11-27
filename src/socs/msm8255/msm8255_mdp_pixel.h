#pragma once

#include "../../lcd/plane_sample.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

using Msm8255MdpRegisterRead = std::function<uint32_t(uint32_t)>;

using Msm8255MdpSample = PlaneSample;

struct Msm8255MdpPixel {
    uint16_t r, g, b;
    uint8_t  a;
};

inline constexpr uint32_t kMsm8255MdpPixelBits = 12u;

}
