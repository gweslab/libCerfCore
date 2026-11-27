#include "cerf_virt_overlay_plane.h"

#include "../../lcd/csc_matrix.h"
#include "../../lcd/qseed_coefficients.h"
#include "../../lcd/qseed_scaler.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/lcd/qcom_tile_layout.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_fb_regs.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace {

using namespace CerfVirt;

constexpr uint32_t kQuarterTurns = 4u;
constexpr uint32_t kChannelMax   = 0xFFu;
constexpr uint32_t kOpaque       = 0xFF000000u;
constexpr uint32_t kVgMaxScale   = 8u;
constexpr uint32_t kVgFirScale   = 4u;

constexpr uint32_t kGreenOrLuma = 0u;
constexpr uint32_t kBlueOrCb    = 1u;
constexpr uint32_t kRedOrCr     = 2u;
constexpr uint32_t kAlpha       = 3u;

constexpr CscMatrix kBt601Limited = [] {
    CscMatrix m{};
    m.matrix     = {596, 0, 817, 596, -201, -416, 596, 1033, 0};
    m.pre_bias   = {-16, -128, -128};
    m.pre_clamp  = {16, 235, 16, 240, 16, 240};
    m.post_clamp = {0, 255, 0, 255, 0, 255};
    return m;
}();

bool IsYuv(uint32_t format) { return format >= kFbOverlayYuyv; }

bool IsSemiPlanar(uint32_t format) {
    return format == kFbOverlayYuv420SemiPlanar || format == kFbOverlayYuv420SemiPlanarTiled;
}

PlaneLayout Interleaved(uint32_t unit, uint32_t elements, bool alpha, std::array<uint32_t, 4> component,
                        std::array<uint32_t, 4> bits, std::array<uint32_t, 4> shift) {
    PlaneLayout l{};
    l.planes    = kPlanesInterleaved;
    l.unit      = unit;
    l.elements  = elements;
    l.alpha     = alpha;
    l.component = component;
    l.bits      = bits;
    l.shift     = shift;
    l.pixel     = {kPlaneEveryPixel, kPlaneEveryPixel, kPlaneEveryPixel, kPlaneEveryPixel};
    return l;
}

PlaneLayout Packed422(std::array<uint32_t, 4> component, std::array<uint32_t, 4> pixel) {
    PlaneLayout l = Interleaved(4u, 4u, false, component, {8u, 8u, 8u, 8u}, {0u, 8u, 16u, 24u});
    l.chroma_h = 1u;
    l.pixel    = pixel;
    return l;
}

PlaneLayout Yuv420(uint32_t planes, std::array<uint32_t, 4> component) {
    PlaneLayout l{};
    l.planes    = planes;
    l.elements  = 2u;
    l.chroma_h  = 1u;
    l.chroma_v  = 1u;
    l.component = component;
    l.bits      = {8u, 8u, 8u, 8u};
    l.shift     = {0u, 8u, 0u, 0u};
    l.pixel     = {kPlaneEveryPixel, kPlaneEveryPixel, kPlaneEveryPixel, kPlaneEveryPixel};
    return l;
}

PlaneLayout LayoutFor(const CerfVirtOverlay& o) {
    const uint32_t B = kBlueOrCb, G = kGreenOrLuma, R = kRedOrCr, A = kAlpha;
    switch (o.format) {
    case kFbOverlayRgb565:   return Interleaved(2u, 3u, false, {B, G, R, 0u}, {5u, 6u, 5u, 0u}, {0u, 5u, 11u, 0u});
    case kFbOverlayRgb888:   return Interleaved(3u, 3u, false, {B, G, R, 0u}, {8u, 8u, 8u, 0u}, {0u, 8u, 16u, 0u});
    case kFbOverlayArgb8888: return Interleaved(4u, 4u, true, {B, G, R, A}, {8u, 8u, 8u, 8u}, {0u, 8u, 16u, 24u});
    case kFbOverlayYuyv:     return Packed422({G, B, G, R}, {0u, kPlaneEveryPixel, 1u, kPlaneEveryPixel});
    case kFbOverlayUyvy:     return Packed422({B, G, R, G}, {kPlaneEveryPixel, 0u, kPlaneEveryPixel, 1u});
    case kFbOverlayYuv420Planar: return Yuv420(kPlanesPlanar, {B, R, 0u, 0u});
    default: {
        PlaneLayout l = Yuv420(kPlanesPseudoPlanar, o.cr_offset > o.cb_offset
                                                        ? std::array<uint32_t, 4>{B, R, 0u, 0u}
                                                        : std::array<uint32_t, 4>{R, B, 0u, 0u});
        l.tiled = o.format == kFbOverlayYuv420SemiPlanarTiled;
        return l;
    }
    }
}

uint32_t HalfUp(uint32_t v) { return (v + 1u) >> 1; }

bool VgScaleSupported(uint32_t src, uint32_t dst) {
    return dst <= kVgMaxScale * src && kVgMaxScale * dst >= src;
}

QseedScaler::Axis VgAxis(uint32_t src, uint32_t dst, uint32_t select) {
    if (src == dst) return {QseedScaler::kUnitPixelRepeat, kPlanePhaseOne, false, {}};
    const uint32_t unit = kVgFirScale * dst >= src ? QseedScaler::kUnitFir : QseedScaler::kUnitMn;
    return {unit, static_cast<uint32_t>((uint64_t{src} << kPlanePhaseBits) / dst), false,
            QseedScaler::SelectTaps([](uint32_t word) { return kQseedCoefficients[word]; }, select)};
}

}

bool CerfVirtOverlayPlane::ShouldRegister() { return emu_.Get<DeviceConfig>().guest_additions; }

bool CerfVirtOverlayPlane::Backed(uint32_t pa, uint64_t bytes) {
    return emu_.Get<CerfVirtFramebuffer>().InRegion(pa, bytes) ||
           emu_.Get<EmulatedMemory>().TryTranslateRange(pa, bytes) != nullptr;
}

void CerfVirtOverlayPlane::Read(uint32_t pa, uint8_t* dst, uint32_t bytes) {
    auto& fb = emu_.Get<CerfVirtFramebuffer>();
    if (fb.InRegion(pa, bytes)) {
        std::memcpy(dst, fb.Bytes() + (pa - fb.MemBasePa()), bytes);
        return;
    }
    emu_.Get<EmulatedMemory>().CopyOut(pa, dst, bytes);
}

PlaneSource CerfVirtOverlayPlane::Source(const CerfVirtOverlay& o) {
    PlaneSource s;
    s.layout  = LayoutFor(o);
    s.base    = {o.pa, o.pa + (IsSemiPlanar(o.format) ? std::min(o.cb_offset, o.cr_offset) : o.cb_offset),
                 o.pa + o.cr_offset};
    s.stride  = {o.stride, o.chroma_stride, o.chroma_stride};
    s.x       = o.src_left;
    s.y       = o.src_top;
    s.w       = o.src_right - o.src_left;
    s.h       = o.src_bottom - o.src_top;
    s.frame_w = o.width;
    s.frame_h = o.height;
    return s;
}

void CerfVirtOverlayPlane::Validate(const CerfVirtOverlay& o) {
    auto& fatal = emu_.Get<Fatal>();
    if (o.layers > kFbOverlayLayersBoth)
        fatal.Die("[CerfVirtOverlayPlane] layer selection %u", o.layers);
    if (o.layers == kFbOverlayLayersBase) return;
    if (o.format < kFbOverlayRgb565 || o.format > kFbOverlayYuv420SemiPlanarTiled)
        fatal.Die("[CerfVirtOverlayPlane] overlay format %u", o.format);
    if (o.width == 0u || o.height == 0u || o.src_left >= o.src_right || o.src_right > o.width ||
        o.src_top >= o.src_bottom || o.src_bottom > o.height || o.dst_left >= o.dst_right ||
        o.dst_top >= o.dst_bottom || o.clockwise >= kQuarterTurns)
        fatal.Die("[CerfVirtOverlayPlane] overlay %ux%u src (%u,%u)-(%u,%u) dst (%u,%u)-(%u,%u) turn %u", o.width,
                  o.height, o.src_left, o.src_top, o.src_right, o.src_bottom, o.dst_left, o.dst_top, o.dst_right,
                  o.dst_bottom, o.clockwise);
    const bool     quarter = (o.clockwise & 1u) != 0u;
    const uint32_t src_w = o.src_right - o.src_left, src_h = o.src_bottom - o.src_top;
    if (!VgScaleSupported(quarter ? src_h : src_w, o.dst_right - o.dst_left) ||
        !VgScaleSupported(quarter ? src_w : src_h, o.dst_bottom - o.dst_top))
        fatal.Die("[CerfVirtOverlayPlane] overlay scales %ux%u (turn %u) to %ux%u past 8x", src_w, src_h, o.clockwise,
                  o.dst_right - o.dst_left, o.dst_bottom - o.dst_top);
    const PlaneLayout l = LayoutFor(o);
    bool backed = false;
    if (l.tiled) {
        const uint32_t cols = qcom_tile::Columns(o.width);
        backed = (o.cr_offset == o.cb_offset + 1u || o.cb_offset == o.cr_offset + 1u) &&
                 Backed(o.pa, uint64_t{cols} * qcom_tile::Rows(o.height) * qcom_tile::kBytes) &&
                 Backed(o.pa + std::min(o.cb_offset, o.cr_offset),
                        uint64_t{cols} * qcom_tile::Rows(o.height >> 1) * qcom_tile::kBytes);
    } else if (l.planes == kPlanesInterleaved) {
        const uint64_t row = (uint64_t{o.width} >> l.chroma_h) * l.unit + (o.width & l.chroma_h) * l.unit;
        backed = o.stride >= row && Backed(o.pa, uint64_t{o.stride} * o.height);
    } else {
        const uint64_t chroma_rows  = HalfUp(o.height);
        const uint64_t chroma_row   = uint64_t{HalfUp(o.width)} * (l.planes == kPlanesPseudoPlanar ? 2u : 1u);
        const bool     semi_ordered = l.planes != kPlanesPseudoPlanar || o.cr_offset == o.cb_offset + 1u ||
                                  o.cb_offset == o.cr_offset + 1u;
        backed = semi_ordered && o.stride >= o.width && o.chroma_stride >= chroma_row &&
                 Backed(o.pa, uint64_t{o.stride} * o.height) &&
                 Backed(o.pa + std::min(o.cb_offset, o.cr_offset), o.chroma_stride * chroma_rows) &&
                 Backed(o.pa + std::max(o.cb_offset, o.cr_offset), o.chroma_stride * chroma_rows);
    }
    if (!backed)
        fatal.Die("[CerfVirtOverlayPlane] overlay format %u %ux%u at 0x%08X stride %u, Cb +0x%X Cr +0x%X stride %u "
                  "is not backed guest memory", o.format, o.width, o.height, o.pa, o.stride, o.cb_offset,
                  o.cr_offset, o.chroma_stride);
}

void CerfVirtOverlayPlane::Turn(uint32_t clockwise, uint32_t& w, uint32_t& h) {
    if (clockwise == 0u) {
        turned_.swap(samples_);
        return;
    }
    const uint32_t tw = (clockwise & 1u) != 0u ? h : w;
    const uint32_t th = (clockwise & 1u) != 0u ? w : h;
    turned_.resize(size_t(tw) * th);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t tx = clockwise == 1u ? h - 1u - y : clockwise == 2u ? w - 1u - x : y;
            const uint32_t ty = clockwise == 1u ? x : clockwise == 2u ? h - 1u - y : w - 1u - x;
            turned_[size_t(ty) * tw + tx] = samples_[size_t(y) * w + x];
        }
    }
    w = tw;
    h = th;
}

void CerfVirtOverlayPlane::Compose(const CerfVirtOverlay& o, uint32_t* dib, uint32_t dib_w, uint32_t dib_h) {
    if (o.layers == kFbOverlayLayersBase) return;
    auto& fetch = emu_.Get<PlaneFetch>();
    const PlaneSource src = Source(o);
    fetch.Fetch(src, [this](uint32_t pa, uint8_t* dst, uint32_t bytes) { Read(pa, dst, bytes); }, scratch_,
                samples_);
    uint32_t w = src.w, h = src.h;
    Turn(o.clockwise, w, h);
    const uint32_t dst_w = o.dst_right - o.dst_left, dst_h = o.dst_bottom - o.dst_top;
    emu_.Get<QseedScaler>().Scale(turned_, w, h, VgAxis(w, dst_w, o.select_x), VgAxis(h, dst_h, o.select_y), dst_w,
                                  dst_h, columns_, scaled_);
    const bool yuv  = IsYuv(o.format);
    const bool both = o.layers == kFbOverlayLayersBoth;
    for (uint32_t y = 0; y < dst_h && o.dst_top + y < dib_h; ++y) {
        uint32_t* row = dib + size_t(o.dst_top + y) * dib_w;
        for (uint32_t x = 0; x < dst_w && o.dst_left + x < dib_w; ++x) {
            const PlaneSample s = scaled_[size_t(y) * dst_w + x];
            const std::array<uint32_t, 3> rgb =
                yuv ? CscConvert(kBt601Limited, {s.c0, s.c1, s.c2}) : std::array<uint32_t, 3>{s.c2, s.c0, s.c1};
            uint32_t& px = row[o.dst_left + x];
            if (!both) {
                px = kOpaque | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
                continue;
            }
            const uint8_t inv = static_cast<uint8_t>(kChannelMax - (px >> 24));
            const uint32_t r  = PlaneMix(uint16_t(rgb[0]), inv, uint16_t((px >> 16) & kChannelMax), kChannelMax,
                                         kChannelMax);
            const uint32_t g  = PlaneMix(uint16_t(rgb[1]), inv, uint16_t((px >> 8) & kChannelMax), kChannelMax,
                                         kChannelMax);
            const uint32_t b  = PlaneMix(uint16_t(rgb[2]), inv, uint16_t(px & kChannelMax), kChannelMax, kChannelMax);
            px = kOpaque | (r << 16) | (g << 8) | b;
        }
    }
}

REGISTER_SERVICE(CerfVirtOverlayPlane);

}
