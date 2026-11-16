#include "imx6_vivante_rs.h"

namespace cerfcore {

namespace imx6_vivante {

namespace {

constexpr uint32_t kRsConfig = 0x01604u;
constexpr uint32_t kRsSourceAddr = 0x01608u;
constexpr uint32_t kRsSourceStride = 0x0160Cu;
constexpr uint32_t kRsDestAddr = 0x01610u;
constexpr uint32_t kRsDestStride = 0x01614u;
constexpr uint32_t kRsWindowSize = 0x01620u;
constexpr uint32_t kRsClearControl = 0x0163Cu;
constexpr uint32_t kRsFillValue0 = 0x01640u;
constexpr uint32_t kRsKickerInPlace = 0x016B0u;
constexpr uint32_t kRsPipeSourceAddr = 0x016C0u;
constexpr uint32_t kRsPipeDestAddr = 0x016E0u;
constexpr uint32_t kRsPipeOffset = 0x01700u;

}

void VivanteRs::ExecuteRsInPlace(uint32_t tile_count) {
    mem_.HaltUnsupported("imx6-vivante unsupported RS.KICKER_INPLACE", kRsKickerInPlace, tile_count);
}

static bool IsModelledRsFormat(uint32_t fmt) { return fmt <= 7u; }

void VivanteRs::ExecuteRs() {
    const uint32_t cfg = mem_.StateReg(kRsConfig);
    if ((cfg & 0x9FFFA000u) != 0u)
        mem_.HaltUnsupported("kRsConfigReservedBits", kRsConfig, cfg);
    const uint32_t src_fmt = cfg & 0x1Fu;
    const uint32_t dst_fmt = (cfg >> 8) & 0x1Fu;
    const bool downsample_x = (cfg & (1u << 5)) != 0u;
    const bool downsample_y = (cfg & (1u << 6)) != 0u;
    const bool src_cfg_tiled = (cfg & (1u << 7)) != 0u;
    const bool dst_cfg_tiled = (cfg & (1u << 14)) != 0u;
    const bool swap_rb = (cfg & (1u << 29)) != 0u;
    const bool flip_y = (cfg & (1u << 30)) != 0u;

    const uint32_t src_addr = mem_.StateReg(kRsSourceAddr);
    const uint32_t src_stride_reg = mem_.StateReg(kRsSourceStride);
    const uint32_t src_stride = src_stride_reg & 0x3FFFFu;
    const uint32_t dst_addr = mem_.StateReg(kRsDestAddr);
    const uint32_t dst_stride_reg = mem_.StateReg(kRsDestStride);
    const uint32_t dst_stride = dst_stride_reg & 0x3FFFFu;
    const uint32_t wh = mem_.StateReg(kRsWindowSize);
    const uint32_t window_w = Lo16(wh);
    const uint32_t window_h = Hi16(wh);
    const uint32_t clear_control = mem_.StateReg(kRsClearControl);
    const uint32_t clear_bits = clear_control & 0xFFFFu;
    const uint32_t clear_mode = (clear_control >> 16) & 3u;
    if (clear_mode == 3u)
        mem_.HaltUnsupported("imx6-vivante RS CLEAR_CONTROL ENABLED4_2", kRsClearControl, clear_control);
    const uint32_t src_bpp = BppFromDeFormat(src_fmt);
    const uint32_t dst_bpp = BppFromDeFormat(dst_fmt);

    if (!IsModelledRsFormat(dst_fmt) || dst_bpp == 0u) {
        mem_.HaltUnsupported("imx6-vivante unsupported RS destination format", kRsConfig, dst_fmt);
    }
    if (!IsModelledRsFormat(src_fmt) || src_bpp == 0u) {
        mem_.HaltUnsupported("imx6-vivante unsupported RS source format", kRsConfig, src_fmt);
    }

    if ((src_stride_reg & (1u << 27)) != 0u)
        mem_.HaltUnsupported("imx6-vivante RS source SUPER_TILED_NEW on a core without CACHE128B256BPERLINE",
                             kRsSourceStride, src_stride_reg);
    if ((dst_stride_reg & (1u << 27)) != 0u)
        mem_.HaltUnsupported("imx6-vivante RS destination SUPER_TILED_NEW on a core without CACHE128B256BPERLINE",
                             kRsDestStride, dst_stride_reg);
    const bool src_supertiled = (src_stride_reg & (1u << 31)) != 0u;
    const bool dst_supertiled = (dst_stride_reg & (1u << 31)) != 0u;
    const bool src_tiled = src_cfg_tiled || src_supertiled;
    const bool dst_tiled = dst_cfg_tiled || dst_supertiled;
    const bool src_multi = (src_stride_reg & (1u << 30)) != 0u;
    const bool dst_multi = (dst_stride_reg & (1u << 30)) != 0u;

    if (window_w == 0u || window_h == 0u)
        mem_.HaltUnsupported("kRsWindowExtent", kRsWindowSize, wh);
    if (clear_mode == 0u && src_stride == 0u)
        mem_.HaltUnsupported("imx6-vivante RS source with a zero stride", kRsSourceStride, src_stride_reg);
    if (dst_stride == 0u)
        mem_.HaltUnsupported("imx6-vivante RS destination with a zero stride", kRsDestStride, dst_stride_reg);

    if (src_multi && mem_.PixelPipes() < 2u)
        mem_.HaltUnsupported("imx6-vivante RS source MULTI requested on a one-pipe core", kRsSourceStride,
                             src_stride_reg);
    if (dst_multi && mem_.PixelPipes() < 2u)
        mem_.HaltUnsupported("imx6-vivante RS destination MULTI requested on a one-pipe core", kRsDestStride,
                             dst_stride_reg);

    uint32_t pipe_src_addr[2] = {mem_.StateReg(kRsPipeSourceAddr), mem_.StateReg(kRsPipeSourceAddr + 4u)};
    uint32_t pipe_dst_addr[2] = {mem_.StateReg(kRsPipeDestAddr), mem_.StateReg(kRsPipeDestAddr + 4u)};
    const uint32_t pipe_offset_reg[2] = {mem_.StateReg(kRsPipeOffset), mem_.StateReg(kRsPipeOffset + 4u)};
    const uint32_t pipe_offset_x[2] = {Lo16(pipe_offset_reg[0]), Lo16(pipe_offset_reg[1])};
    const uint32_t pipe_offset_y[2] = {Hi16(pipe_offset_reg[0]), Hi16(pipe_offset_reg[1])};

    const uint32_t pipe_count = mem_.PixelPipes();
    if (pipe_count > 2u) mem_.HaltUnsupported("imx6-vivante unsupported RS pixel pipe count", kRsConfig, pipe_count);
    if (!src_multi) {
        pipe_src_addr[0] = src_addr;
        pipe_src_addr[1] = src_addr;
    }
    if (!dst_multi) {
        pipe_dst_addr[0] = dst_addr;
        pipe_dst_addr[1] = dst_addr;
    }

    for (uint32_t p = 0; p < pipe_count; ++p) {
        if (clear_mode == 0u && (pipe_src_addr[p] == 0u || !mem_.TranslateGpuToHost(pipe_src_addr[p], 1u))) {
            mem_.HaltUnsupported("imx6-vivante RS source outside GPU-mapped memory", kRsSourceAddr, pipe_src_addr[p]);
        }
        if (pipe_dst_addr[p] == 0u || !mem_.TranslateGpuToHostWrite(pipe_dst_addr[p], 1u)) {
            mem_.HaltUnsupported("imx6-vivante RS destination outside GPU-mapped memory", kRsDestAddr,
                                 pipe_dst_addr[p]);
        }
    }

    const uint32_t scale_x = (clear_mode == 0u && downsample_x) ? 2u : 1u;
    const uint32_t scale_y = (clear_mode == 0u && downsample_y) ? 2u : 1u;
    const uint32_t output_w = window_w / scale_x;
    const uint32_t output_h = window_h / scale_y;
    if (output_w == 0u || output_h == 0u)
        mem_.HaltUnsupported("kRsOutputExtent", kRsWindowSize, wh);

    uint32_t source_full_height = 0u;
    for (uint32_t p = 0; p < pipe_count; ++p) {
        const uint32_t bottom = pipe_offset_y[p] + window_h;
        if (bottom > source_full_height) source_full_height = bottom;
    }

    auto read_source_surface = [&](uint32_t base, uint32_t x, uint32_t y, uint32_t& argb) {
        if (!ReadSurfacePixelGpu(base, src_stride, x, y, src_fmt, kRsConfig, 0u, src_tiled, src_supertiled, argb,
                                 MmuClient::Rasterizer)) {
            mem_.HaltUnsupported("imx6-vivante RS source pixel read outside GPU-mapped memory", base,
                                 (static_cast<uint64_t>(y) << 32) | x);
        }
    };

    auto read_source_global = [&](uint32_t gx, uint32_t gy, uint32_t& argb) {
        if (!src_multi) {
            read_source_surface(pipe_src_addr[0], gx, gy, argb);
            return;
        }
        for (uint32_t p = 0; p < pipe_count; ++p) {
            if (gx < pipe_offset_x[p] || gy < pipe_offset_y[p] || gx >= pipe_offset_x[p] + window_w ||
                gy >= pipe_offset_y[p] + window_h) {
                continue;
            }
            read_source_surface(pipe_src_addr[p], gx - pipe_offset_x[p], gy - pipe_offset_y[p], argb);
            return;
        }
        mem_.HaltUnsupported("kRsSourceOutsideConfiguredPipes", kRsPipeOffset,
                             (static_cast<uint64_t>(gy) << 32) | gx);
    };

    auto swap_red_blue = [](uint32_t argb) -> uint32_t {
        return (argb & 0xFF00FF00u) | ((argb & 0x000000FFu) << 16) | ((argb & 0x00FF0000u) >> 16);
    };

    auto merge_clear_channels = [](uint32_t old_argb, uint32_t fill_argb, uint32_t nibble) -> uint32_t {
        uint32_t mask = 0u;
        if (nibble & 0x1u) mask |= 0x000000FFu;
        if (nibble & 0x2u) mask |= 0x0000FF00u;
        if (nibble & 0x4u) mask |= 0x00FF0000u;
        if (nibble & 0x8u) mask |= 0xFF000000u;
        return (old_argb & ~mask) | (fill_argb & mask);
    };

    for (uint32_t p = 0; p < pipe_count; ++p) {
        const uint32_t dst_origin_x = pipe_offset_x[p] / scale_x;
        const uint32_t dst_origin_y = pipe_offset_y[p] / scale_y;
        for (uint32_t dy = 0; dy < output_h; ++dy) {
            for (uint32_t dx = 0; dx < output_w; ++dx) {
                const uint32_t global_dx = dst_origin_x + dx;
                const uint32_t global_dy = dst_origin_y + dy;
                const uint32_t write_x = dst_multi ? dx : global_dx;
                const uint32_t write_y = dst_multi ? dy : global_dy;
                uint32_t out_argb = 0u;

                if (clear_mode != 0u) {
                    const uint32_t group = clear_mode == 1u ? 0u : ((global_dx >> 2) & 3u);
                    const uint32_t nibble = (clear_bits >> (group * 4u)) & 0xFu;
                    if (nibble == 0u) continue;
                    out_argb = UnpackSurfaceColor(mem_.StateReg(kRsFillValue0 + group * 4u), src_fmt);
                    if (nibble != 0xFu) {
                        uint32_t old_argb = 0u;
                        if (!ReadSurfacePixelGpu(pipe_dst_addr[p], dst_stride, write_x, write_y, dst_fmt, kRsConfig, 0u,
                                                 dst_tiled, dst_supertiled, old_argb,
                                                 MmuClient::PixelEngine)) {
                            mem_.HaltUnsupported("imx6-vivante RS destination pixel read outside GPU-mapped memory",
                                                 pipe_dst_addr[p], (static_cast<uint64_t>(write_y) << 32) | write_x);
                        }
                        out_argb = merge_clear_channels(old_argb, out_argb, nibble);
                    }
                } else {
                    uint32_t sums[4] = {0u, 0u, 0u, 0u};
                    uint32_t samples = 0u;
                    for (uint32_t sy = 0; sy < scale_y; ++sy) {
                        for (uint32_t sx = 0; sx < scale_x; ++sx) {
                            const uint32_t global_sx = pipe_offset_x[p] + dx * scale_x + sx;
                            uint32_t global_sy = pipe_offset_y[p] + dy * scale_y + sy;
                            if (flip_y) global_sy = source_full_height - 1u - global_sy;
                            uint32_t sample = 0u;
                            read_source_global(global_sx, global_sy, sample);
                            sums[0] += (sample >> 24) & 0xFFu;
                            sums[1] += (sample >> 16) & 0xFFu;
                            sums[2] += (sample >> 8) & 0xFFu;
                            sums[3] += sample & 0xFFu;
                            ++samples;
                        }
                    }
                    out_argb = (((sums[0] + samples / 2u) / samples) << 24) |
                               (((sums[1] + samples / 2u) / samples) << 16) |
                               (((sums[2] + samples / 2u) / samples) << 8) | ((sums[3] + samples / 2u) / samples);
                    if (swap_rb) out_argb = swap_red_blue(out_argb);
                }

                if (!WriteSurfacePixelGpu(pipe_dst_addr[p], dst_stride, write_x, write_y, dst_fmt, kRsConfig, 0u, dst_tiled,
                                          dst_supertiled, out_argb)) {
                    mem_.HaltUnsupported("imx6-vivante RS destination pixel write outside GPU-mapped memory",
                                         pipe_dst_addr[p], (static_cast<uint64_t>(write_y) << 32) | write_x);
                }
            }
        }
    }
}

}

}
