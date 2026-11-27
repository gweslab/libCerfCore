#pragma once

#include <array>
#include <cstdint>

namespace cerfcore {

struct PlaneSample {
    uint8_t c0, c1, c2, a;
};

inline constexpr uint32_t kPlanesInterleaved  = 0u;
inline constexpr uint32_t kPlanesPlanar       = 1u;
inline constexpr uint32_t kPlanesPseudoPlanar = 2u;

inline constexpr uint32_t kPlaneComponentLuma  = 0u;
inline constexpr uint32_t kPlaneComponentAlpha = 3u;
inline constexpr uint32_t kPlaneEveryPixel     = 2u;
inline constexpr uint8_t  kPlaneAlphaOpaque    = 0xFFu;

struct PlaneLayout {
    uint32_t planes   = 0;
    uint32_t chroma_h = 0;
    uint32_t chroma_v = 0;
    uint32_t unit     = 0;
    uint32_t elements = 0;
    bool     alpha    = false;
    bool     tiled    = false;
    std::array<uint32_t, 4> component{};
    std::array<uint32_t, 4> shift{};
    std::array<uint32_t, 4> bits{};
    std::array<uint32_t, 4> pixel{};
};

inline constexpr uint32_t kPlanePhaseBits = 29u;
inline constexpr uint32_t kPlanePhaseOne  = 1u << kPlanePhaseBits;

inline uint8_t PlaneLerp(uint8_t a, uint8_t b, uint32_t frac) {
    const uint64_t sum = uint64_t(a) * (kPlanePhaseOne - frac) + uint64_t(b) * frac + kPlanePhaseOne / 2u;
    return static_cast<uint8_t>(sum >> kPlanePhaseBits);
}

inline PlaneSample PlaneInterpolate(const PlaneSample& a, const PlaneSample& b, uint32_t frac) {
    return PlaneSample{PlaneLerp(a.c0, b.c0, frac), PlaneLerp(a.c1, b.c1, frac),
                       PlaneLerp(a.c2, b.c2, frac), PlaneLerp(a.a, b.a, frac)};
}

inline uint16_t PlaneMix(uint16_t fg, uint8_t fa, uint16_t bg, uint8_t ba, uint32_t max) {
    const uint32_t sum = uint32_t(fg) * fa + uint32_t(bg) * ba + 0xFFu / 2u;
    const uint32_t v   = sum / 0xFFu;
    return static_cast<uint16_t>(v < max ? v : max);
}

}
