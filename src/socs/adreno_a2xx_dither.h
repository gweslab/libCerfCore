#pragma once
#include "adreno_a2xx_float.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cerfcore {

inline const std::array<double, 16> kAdrenoA2xxDitherThresholds = [] {
    constexpr uint8_t thresholds[4][4] = {{1,9,3,11},{13,5,15,7},{4,12,2,10},{16,8,14,6}};
    std::array<double, 16> values{};
    for (unsigned y = 0; y < 4u; ++y)
        for (unsigned x = 0; x < 4u; ++x) values[y * 4u + x] = thresholds[y][x] / 17.0;
    return values;
}();

inline uint32_t AdrenoA2xxApproximateDitherQuantize(float channel, uint32_t maximum, unsigned component,
                                          uint32_t x, uint32_t y, bool dithered) {
    const float scaled = channel * maximum;
    if (!dithered || component == 3u) {
        if (!AdrenoA2xxFinite(scaled)) return static_cast<uint32_t>(std::lround(scaled));
        const double value = scaled;
        return static_cast<uint32_t>(static_cast<long>(value >= 0.0 ? std::floor(value + 0.5)
                                                                    : -std::floor(0.5 - value)));
    }
    const double threshold = kAdrenoA2xxDitherThresholds[(y & 3u) * 4u + (x & 3u)];
    return static_cast<uint32_t>(std::clamp(std::floor(scaled + 1.0 - threshold), 0.0, double(maximum)));
}

}
