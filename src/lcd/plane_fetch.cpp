#include "plane_fetch.h"

#include "../core/parallel_jobs.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>
#include <cerfcore/lcd/qcom_tile_layout.h>

#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint32_t kRowBandsPerLane = 4u;

uint8_t Widen8(uint32_t value, uint32_t bits) {
    switch (bits) {
    case 1u: return value != 0u ? kPlaneAlphaOpaque : uint8_t{0};
    case 4u: return static_cast<uint8_t>(lcd_pixel::Expand4(value));
    case 5u: return static_cast<uint8_t>(lcd_pixel::Expand5(value));
    case 6u: return static_cast<uint8_t>(lcd_pixel::Expand6(value));
    default: return static_cast<uint8_t>(value);
    }
}

uint8_t& Slot(PlaneSample& s, uint32_t component) {
    switch (component) {
    case 0u: return s.c0;
    case 1u: return s.c1;
    case 2u: return s.c2;
    default: return s.a;
    }
}

}

PlaneSample PlaneFetch::Unpack(const uint8_t* src, const PlaneLayout& l, uint32_t phase) {
    PlaneSample s{0u, 0u, 0u, kPlaneAlphaOpaque};
    const uint64_t packed = cerf::le::UN(src, l.unit);
    for (uint32_t i = 0; i < l.elements; ++i) {
        if (l.pixel[i] != kPlaneEveryPixel && l.pixel[i] != phase) continue;
        const uint32_t n = l.bits[i];
        Slot(s, l.component[i]) = Widen8(static_cast<uint32_t>(packed >> l.shift[i]) & ((1u << n) - 1u), n);
    }
    if (!l.alpha) s.a = kPlaneAlphaOpaque;
    return s;
}

void PlaneFetch::Fetch(const PlaneSource& src, const PlaneMemoryRead& read, Scratch& scratch,
                       std::vector<PlaneSample>& out) {
    const PlaneLayout& l = src.layout;
    const uint32_t w = src.w, h = src.h, sx = src.x, sy = src.y;
    out.resize(size_t(w) * h);
    if (l.planes == kPlanesInterleaved) {
        const uint32_t ux0   = sx >> l.chroma_h;
        const uint32_t units = ((sx + w - 1u) >> l.chroma_h) - ux0 + 1u;
        auto& jobs = emu_.Get<ParallelJobs>();
        const uint32_t bands = std::min(h, jobs.Lanes() * kRowBandsPerLane);
        if (scratch.band_rows.size() < bands) scratch.band_rows.resize(bands);
        const std::function<void(uint32_t)> band = [&](uint32_t index) {
            std::vector<uint8_t>& row = scratch.band_rows[index];
            row.resize(size_t(units) * l.unit);
            const uint32_t y_end = static_cast<uint32_t>(uint64_t(h) * (index + 1u) / bands);
            for (uint32_t y = static_cast<uint32_t>(uint64_t(h) * index / bands); y < y_end; ++y) {
                read(src.base[0] + (sy + y) * src.stride[0] + ux0 * l.unit, row.data(),
                     static_cast<uint32_t>(row.size()));
                for (uint32_t x = 0; x < w; ++x) {
                    const uint32_t px = sx + x;
                    out[size_t(y) * w + x] =
                        Unpack(row.data() + size_t((px >> l.chroma_h) - ux0) * l.unit, l, px & l.chroma_h);
                }
            }
        };
        jobs.Run(bands, band);
        return;
    }
    const uint32_t cx0 = sx >> l.chroma_h;
    const uint32_t cw  = ((sx + w - 1u) >> l.chroma_h) - cx0 + 1u;
    const bool pseudo  = l.planes == kPlanesPseudoPlanar;
    std::array<std::vector<uint8_t>, 3> rows;
    rows[0].resize(w);
    rows[1].resize(size_t(cw) * (pseudo ? l.elements : 1u));
    rows[2].resize(pseudo ? 0u : cw);
    const uint32_t tile_cols   = qcom_tile::Columns(src.frame_w);
    const uint32_t luma_rows   = qcom_tile::Rows(src.frame_h);
    const uint32_t chroma_rows = qcom_tile::Rows(src.frame_h >> 1);
    const auto read_row = [&](uint32_t plane, uint32_t row, uint32_t x0, uint32_t bytes,
                              uint32_t tile_rows, uint8_t* dest) {
        if (!l.tiled) {
            read(src.base[plane] + row * src.stride[plane] + x0, dest, bytes);
            return;
        }
        for (uint32_t done = 0; done < bytes;) {
            const uint32_t x   = x0 + done;
            const uint32_t run = std::min(bytes - done, qcom_tile::kWidth - x % qcom_tile::kWidth);
            read(src.base[plane] + qcom_tile::Offset(x, row, tile_cols, tile_rows), dest + done, run);
            done += run;
        }
    };
    for (uint32_t y = 0; y < h; ++y) {
        const uint32_t py = sy + y, cy = py >> l.chroma_v;
        read_row(0u, py, sx, w, luma_rows, rows[0].data());
        read_row(1u, cy, cx0 * (pseudo ? l.elements : 1u), static_cast<uint32_t>(rows[1].size()),
                 chroma_rows, rows[1].data());
        if (!pseudo) read_row(2u, cy, cx0, cw, chroma_rows, rows[2].data());
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t cx = ((sx + x) >> l.chroma_h) - cx0;
            PlaneSample s{rows[0][x], 0u, 0u, kPlaneAlphaOpaque};
            for (uint32_t i = 0; i < l.elements && i < 2u; ++i) {
                const uint32_t byte = pseudo ? rows[1][size_t(cx) * l.elements + i] : rows[1 + i][cx];
                Slot(s, l.component[i]) =
                    Widen8((byte >> (l.shift[i] - 8u * i)) & ((1u << l.bits[i]) - 1u), l.bits[i]);
            }
            out[size_t(y) * w + x] = s;
        }
    }
}

void PlaneFetch::ResampleBilinear(const std::vector<PlaneSample>& src, uint32_t src_w, uint32_t src_h,
                                  uint32_t dst_w, uint32_t dst_h, uint32_t step_x, uint32_t step_y,
                                  bool lerp, bool flip_x, bool flip_y, std::vector<PlaneSample>& out) {
    out.resize(size_t(dst_w) * dst_h);
    auto& jobs = emu_.Get<ParallelJobs>();
    const uint32_t bands = std::min(dst_h, jobs.Lanes() * kRowBandsPerLane);
    const std::function<void(uint32_t)> band = [&](uint32_t index) {
        const uint32_t y_end = static_cast<uint32_t>(uint64_t(dst_h) * (index + 1u) / bands);
        for (uint32_t y = static_cast<uint32_t>(uint64_t(dst_h) * index / bands); y < y_end; ++y) {
            const uint64_t py  = uint64_t(y) * step_y;
            uint32_t sy0       = std::min<uint32_t>(static_cast<uint32_t>(py >> kPlanePhaseBits), src_h - 1u);
            uint32_t sy1       = std::min(sy0 + 1u, src_h - 1u);
            const uint32_t fy  = lerp ? static_cast<uint32_t>(py & (kPlanePhaseOne - 1u)) : 0u;
            if (flip_y) {
                sy0 = src_h - 1u - sy0;
                sy1 = src_h - 1u - sy1;
            }
            for (uint32_t x = 0; x < dst_w; ++x) {
                const uint64_t px  = uint64_t(x) * step_x;
                uint32_t sx0       = std::min<uint32_t>(static_cast<uint32_t>(px >> kPlanePhaseBits), src_w - 1u);
                uint32_t sx1       = std::min(sx0 + 1u, src_w - 1u);
                const uint32_t fx  = lerp ? static_cast<uint32_t>(px & (kPlanePhaseOne - 1u)) : 0u;
                if (flip_x) {
                    sx0 = src_w - 1u - sx0;
                    sx1 = src_w - 1u - sx1;
                }
                const PlaneSample* r0 = src.data() + size_t(sy0) * src_w;
                const PlaneSample* r1 = src.data() + size_t(sy1) * src_w;
                const PlaneSample top = fx ? PlaneInterpolate(r0[sx0], r0[sx1], fx) : r0[sx0];
                const PlaneSample bot = fx ? PlaneInterpolate(r1[sx0], r1[sx1], fx) : r1[sx0];
                out[size_t(y) * dst_w + x] = fy ? PlaneInterpolate(top, bot, fy) : top;
            }
        }
    };
    jobs.Run(bands, band);
}

REGISTER_SERVICE(PlaneFetch);

}
