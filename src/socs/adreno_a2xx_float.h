#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace cerfcore {

inline bool AdrenoA2xxFinite(float value) {
    return (std::bit_cast<uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
}

inline bool AdrenoA2xxFinite(double value) {
    return (std::bit_cast<uint64_t>(value) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

inline float AdrenoA2xxLerp(float a, float b, float t) {
    if (!AdrenoA2xxFinite(a) || !AdrenoA2xxFinite(b) || !AdrenoA2xxFinite(t)) return std::lerp(a, b, t);
    if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0)) return t * b + (1 - t) * a;
    if (t == 1) return b;
    const float candidate = std::fma(t, b - a, a);
    if ((t > 1) == (b > a)) return b > candidate ? b : candidate;
    return candidate > b ? b : candidate;
}

inline std::array<float, 4> AdrenoA2xxLerpLanes(const std::array<float, 4>& a, const std::array<float, 4>& b,
                                                const std::array<float, 4>& t) {
    return {AdrenoA2xxLerp(a[0], b[0], t[0]), AdrenoA2xxLerp(a[1], b[1], t[1]),
            AdrenoA2xxLerp(a[2], b[2], t[2]), AdrenoA2xxLerp(a[3], b[3], t[3])};
}

}
