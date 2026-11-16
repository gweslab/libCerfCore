#include "imx6_vivante_vr.h"

#include "imx6_vivante_draw2d_regs.h"
#include "imx6_vivante_draw2d_state.h"

namespace cerfcore {

namespace imx6_vivante {

namespace {

constexpr uint32_t kUplaneAddress = 0x01284u;
constexpr uint32_t kUplaneStride = 0x01288u;
constexpr uint32_t kVplaneAddress = 0x0128Cu;
constexpr uint32_t kVplaneStride = 0x01290u;
constexpr uint32_t kVrSourceImageLow = 0x01298u;
constexpr uint32_t kVrSourceImageHigh = 0x0129Cu;
constexpr uint32_t kVrSourceOriginX = 0x012A0u;
constexpr uint32_t kVrSourceOriginY = 0x012A4u;
constexpr uint32_t kVrTargetLow = 0x012A8u;
constexpr uint32_t kVrTargetHigh = 0x012ACu;
constexpr uint32_t kPeControl = 0x012D8u;
constexpr uint32_t kD2dVideoRasterizerStart = 0x01294u;
constexpr uint32_t kVrConfigEx = 0x012E4u;
constexpr uint32_t kSharedKernel = 0x01800u;
constexpr uint32_t kHorizontalKernel = 0x02800u;
constexpr uint32_t kVerticalKernel = 0x02A00u;

}

void VivanteVr::Execute(uint32_t start_value) {
    const uint32_t mode = start_value & 3u;
    if (mode > 2u) {
        mem_.HaltUnsupported("imx6-vivante unsupported VR START", kD2dVideoRasterizerStart, mode);
    }

    const uint32_t src_addr = mem_.StateReg(kD2dSrcAddress);
    const uint32_t src_stride = mem_.StateReg(kD2dSrcStride) & 0x3FFFFu;
    const uint32_t src_cfg = mem_.StateReg(kD2dSrcConfig);
    const uint32_t src_ex_cfg = mem_.StateReg(kD2dSrcExConfig);
    const uint32_t src_ex_addr = mem_.StateReg(kD2dSrcExAddress);
    const uint32_t dst_cfg = mem_.StateReg(kD2dDestConfig);
    const uint32_t raw_dst_stride = mem_.StateReg(kD2dDestStride);
    ValidateDeConfiguration(mem_, src_cfg, src_ex_cfg, dst_cfg, raw_dst_stride);
    const DeSourceConfig src_config = DecodeDeSourceConfig(src_cfg, src_ex_cfg);
    const uint32_t src_fmt = src_config.format;
    const uint32_t src_swizzle = src_config.swizzle;
    const uint32_t src_endian = src_config.endian;
    const bool src_tiled = src_config.tiled;
    const bool src_multi_tiled = src_config.multi_tiled;
    const bool src_supertiled = src_config.supertiled;
    const SurfaceLayout src_layout = DecodeSurfaceLayout(src_tiled, src_multi_tiled, src_supertiled);
    const bool src_yuv = IsVrYuvFormat(src_fmt);
    const uint32_t pe_control = mem_.StateReg(kPeControl);
    const bool yuv_bt709 = (pe_control & 1u) != 0u;
    const bool uv_swizzle = (pe_control & (1u << 4)) != 0u;
    const bool yuv_to_rgb = (pe_control & (1u << 8)) != 0u;
    const bool src_format_supported = src_fmt <= 6u || src_fmt == 9u || src_fmt == 16u || src_yuv;
    const uint32_t src_bpp = src_yuv ? 0u : (src_format_supported ? BppFromDeFormat(src_fmt) : 0u);

    const uint32_t dst_addr = mem_.StateReg(kD2dDestAddress);
    const uint32_t dst_stride = raw_dst_stride & 0x3FFFFu;
    const DeDestinationConfig dst_config = DecodeDeDestinationConfig(dst_cfg);
    const uint32_t dst_fmt = dst_config.format;
    const uint32_t dst_swizzle = dst_config.swizzle;
    const uint32_t dst_endian = dst_config.endian;
    const bool dst_tiled = dst_config.tiled;
    const SurfaceLayout dst_layout = DecodeSurfaceLayout(dst_tiled, false, false);
    const bool dst_format_supported = dst_fmt <= 6u || dst_fmt == 16u;
    const uint32_t dst_bpp = dst_format_supported ? BppFromDeFormat(dst_fmt) : 0u;

    if (!src_format_supported)
        mem_.HaltUnsupported("imx6-vivante unsupported VR source format", kD2dSrcConfig, src_fmt);
    if (!dst_format_supported)
        mem_.HaltUnsupported("imx6-vivante unsupported VR destination format", kD2dDestConfig, dst_fmt);

    if (src_yuv && !yuv_to_rgb) {
        mem_.HaltUnsupported("imx6-vivante VR YUV source without PE_CONTROL.YUVRGB", kPeControl,
                             (static_cast<uint64_t>(src_cfg) << 32) | pe_control);
    }

    const bool planar_yuv = src_fmt == 15u || src_fmt == 17u || src_fmt == 18u;
    if (planar_yuv && IsMultiLayout(src_layout)) {
        mem_.HaltUnsupported("imx6-vivante conflicting/unsupported VR YUV layout", kD2dSrcConfig,
                             (static_cast<uint64_t>(src_ex_cfg) << 32) | src_cfg);
    }

    const bool src_base_valid =
        src_addr && src_stride && (src_bpp || src_yuv) && mem_.TranslateGpuToHost(src_addr, 1u) != nullptr &&
        (!IsMultiLayout(src_layout) || (src_ex_addr != 0u && mem_.TranslateGpuToHost(src_ex_addr, 1u) != nullptr));
    const uint32_t u_addr = mem_.StateReg(kUplaneAddress);
    const uint32_t u_stride = mem_.StateReg(kUplaneStride) & 0x3FFFFu;
    const uint32_t v_addr = mem_.StateReg(kVplaneAddress);
    const uint32_t v_stride = mem_.StateReg(kVplaneStride) & 0x3FFFFu;
    const bool u_base_valid = (u_addr && u_stride) ? mem_.TranslateGpuToHost(u_addr, 1u) != nullptr : false;
    const bool v_base_valid = (v_addr && v_stride) ? mem_.TranslateGpuToHost(v_addr, 1u) != nullptr : false;
    const bool dst_base_valid = dst_addr && dst_stride && dst_bpp && mem_.TranslateGpuToHostWrite(dst_addr, 1u) != nullptr;
    const bool yuv_planes_valid =
        !src_yuv || ((src_fmt == 7u || src_fmt == 8u) || ((src_fmt == 17u || src_fmt == 18u) && u_base_valid) ||
                     (src_fmt == 15u && u_base_valid && v_base_valid));
    if (!src_base_valid || !dst_base_valid || !yuv_planes_valid) {
        mem_.HaltUnsupported("imx6-vivante invalid VR surface", src_addr, dst_addr);
    }

    const uint32_t src_low = mem_.StateReg(kVrSourceImageLow);
    const uint32_t src_high = mem_.StateReg(kVrSourceImageHigh);
    const uint32_t src_left = Lo16(src_low);
    const uint32_t src_top = Hi16(src_low);
    const uint32_t src_right = Lo16(src_high);
    const uint32_t src_bottom = Hi16(src_high);
    const uint32_t target_low = mem_.StateReg(kVrTargetLow);
    const uint32_t target_high = mem_.StateReg(kVrTargetHigh);
    const uint32_t dst_left = Lo16(target_low);
    const uint32_t dst_top = Hi16(target_low);
    const uint32_t dst_right = Lo16(target_high);
    const uint32_t dst_bottom = Hi16(target_high);
    if (src_right <= src_left || src_bottom <= src_top)
        mem_.HaltUnsupported("kVrSourceWindowExtent", kVrSourceImageHigh,
                             (static_cast<uint64_t>(src_high) << 32) | src_low);
    if (dst_right <= dst_left || dst_bottom <= dst_top)
        mem_.HaltUnsupported("kVrDestinationWindowExtent", kVrTargetHigh,
                             (static_cast<uint64_t>(target_high) << 32) | target_low);

    const int64_t origin_x = static_cast<int32_t>(mem_.StateReg(kVrSourceOriginX));
    const int64_t origin_y = static_cast<int32_t>(mem_.StateReg(kVrSourceOriginY));
    const uint32_t factor_x = mem_.StateReg(kD2dStretchX) & 0x7FFFFFFFu;
    const uint32_t factor_y = mem_.StateReg(kD2dStretchY) & 0x7FFFFFFFu;
    const uint32_t step_x = (mode == 0u || mode == 2u) ? factor_x : 0x10000u;
    const uint32_t step_y = (mode == 1u || mode == 2u) ? factor_y : 0x10000u;

    const uint32_t src_rot_cfg = mem_.StateReg(kD2dSrcRotConfig);
    const uint32_t dst_rot_cfg = mem_.StateReg(kD2dDestRotConfig);
    const uint32_t rot_angle = mem_.StateReg(kD2dRotAngle);
    const bool angle_supported = mem_.UsesDeRotationAngle();
    const DeRotation src_rotation = DecodeDeRotation(rot_angle, src_rot_cfg, true, angle_supported);
    const DeRotation dst_rotation = DecodeDeRotation(rot_angle, dst_rot_cfg, false, angle_supported);
    const uint32_t src_rot = src_rotation.mode;
    const uint32_t dst_rot = dst_rotation.mode;
    if (!IsValidDeRot(src_rot))
        mem_.HaltUnsupported("imx6-vivante VR source DE_ROT_MODE encoding is not assigned",
                             angle_supported ? kD2dRotAngle : kD2dSrcRotConfig, src_rot);
    if (!IsValidDeRot(dst_rot))
        mem_.HaltUnsupported("imx6-vivante VR destination DE_ROT_MODE encoding is not assigned",
                             angle_supported ? kD2dRotAngle : kD2dDestRotConfig, dst_rot);
    const uint32_t src_mirror = src_rotation.mirror;
    const uint32_t dst_mirror = dst_rotation.mirror;
    const uint32_t src_surface_w = src_rot_cfg & 0xFFFFu;
    const uint32_t src_surface_h = mem_.StateReg(kD2dSrcRotHeight) & 0xFFFFu;
    const uint32_t dst_surface_w = dst_rot_cfg & 0xFFFFu;
    const uint32_t dst_surface_h = mem_.StateReg(kD2dDestRotHeight) & 0xFFFFu;

    auto floorFixed16 = [](int64_t value) -> int32_t {
        if (value >= 0) return static_cast<int32_t>(value >> 16);
        return -static_cast<int32_t>(((-value) + 0xFFFFll) >> 16);
    };
    auto sourceCoord = [&](uint32_t x, uint32_t y) -> DeCoord {
        return VivanteBlitOps::TransformIfNeeded(x, y, src_surface_w, src_surface_h, src_rot, src_mirror);
    };
    auto destCoord = [&](uint32_t x, uint32_t y) -> DeCoord {
        return VivanteBlitOps::TransformIfNeeded(x, y, dst_surface_w, dst_surface_h, dst_rot, dst_mirror);
    };

    auto haltRead = [&](uint32_t x, uint32_t y) {
        mem_.HaltUnsupported("imx6-vivante VR source pixel read outside GPU-mapped memory", src_addr,
                             (static_cast<uint64_t>(y) << 32) | x);
    };
    auto readSourcePixel = [&](int64_t x, int64_t y, uint32_t& argb) {
        if (x < src_left || x >= src_right || y < src_top || y >= src_bottom)
            mem_.HaltUnsupported("kVrTapOutsideSourceRectangle", src_addr,
                                 (static_cast<uint64_t>(y) << 32) | static_cast<uint32_t>(x));
        const DeCoord p = sourceCoord(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        if (!p.valid)
            mem_.HaltUnsupported("imx6-vivante VR source rotation coordinate outside extent", kD2dSrcRotConfig,
                                 (static_cast<uint64_t>(y) << 32) | static_cast<uint32_t>(x));

        if (src_yuv) {
            if (!ReadVrYuvPixelGpu(src_addr, src_ex_addr, src_stride, u_addr, u_stride, v_addr, v_stride, p.x, p.y,
                                   src_fmt, src_layout, src_endian, uv_swizzle, yuv_bt709, argb))
                haltRead(p.x, p.y);
            return;
        }

        uint32_t packed = 0u;
        if (!ReadLayoutPackedGpu(src_addr, src_ex_addr, src_stride, p.x, p.y, src_bpp, src_layout, src_endian,
                                 packed))
            haltRead(p.x, p.y);
        const uint32_t index_color = (src_fmt & 0x1Fu) == 9u
                                         ? mem_.StateReg(kD2dIndexColorTable32 + (packed & 0xFFu) * 4u)
                                         : 0u;
        argb = DecodeSourcePixel(packed, src_fmt, src_swizzle, mem_.StateReg(kD2dGlobalSrcColor), index_color);
    };
    auto destPixelCoord = [&](uint32_t x, uint32_t y) {
        const DeCoord p = destCoord(x, y);
        if (!p.valid || p.x >= dst_surface_w || p.y >= dst_surface_h)
            mem_.HaltUnsupported("kVrDestinationExtent", kD2dDestRotConfig,
                                 (static_cast<uint64_t>(p.y) << 32) | p.x);
        return p;
    };
    auto readDestPixel = [&](uint32_t x, uint32_t y, uint32_t& argb) {
        const DeCoord p = destPixelCoord(x, y);
        const bool ok = ReadSurfacePixelGpuLayout(dst_addr, dst_stride, p.x, p.y, dst_fmt, kD2dDestConfig,
                                                   dst_swizzle, dst_layout, argb, dst_endian,
                                                   MmuClient::PixelEngine);
        if (!ok)
            mem_.HaltUnsupported("imx6-vivante VR destination pixel read outside GPU-mapped memory", dst_addr,
                                 (static_cast<uint64_t>(p.y) << 32) | p.x);
    };
    auto writeDestPixel = [&](uint32_t x, uint32_t y, uint32_t argb) {
        const DeCoord p = destPixelCoord(x, y);
        const bool ok = WriteSurfacePixelGpuLayout(dst_addr, dst_stride, p.x, p.y, dst_fmt, kD2dDestConfig,
                                                    dst_swizzle, dst_layout, argb, dst_endian);
        if (!ok)
            mem_.HaltUnsupported("imx6-vivante VR destination pixel write outside GPU-mapped memory", dst_addr,
                                 (static_cast<uint64_t>(p.y) << 32) | p.x);
    };

    auto loadKernel = [&](uint32_t table_base, int64_t coordinate, int32_t& sample_base, int16_t out[9]) {
        sample_base = floorFixed16(coordinate);
        const uint32_t phase = (static_cast<uint32_t>(coordinate) >> 11) & 31u;
        uint32_t row = 0u;
        bool reverse_taps = false;
        if (phase <= 16u) {
            row = 16u - phase;
            reverse_taps = true;
        } else {
            ++sample_base;
            row = phase - 16u;
        }
        for (uint32_t tap = 0; tap < 9u; ++tap) {
            const uint32_t stored_tap = reverse_taps ? (8u - tap) : tap;
            const uint32_t index = row * 9u + stored_tap;
            const uint32_t packed = mem_.StateReg(table_base + (index >> 1) * 4u);
            out[tap] = static_cast<int16_t>((index & 1u) ? (packed >> 16) : (packed & 0xFFFFu));
        }
    };
    auto channel = [](uint32_t argb, uint32_t index) -> uint32_t { return (argb >> ((3u - index) * 8u)) & 0xFFu; };
    auto roundShiftSigned = [](int64_t value, uint32_t shift) -> int32_t {
        const int64_t half = 1ll << (shift - 1u);
        if (value >= 0) return static_cast<int32_t>((value + half) >> shift);
        return -static_cast<int32_t>(((-value) + half) >> shift);
    };
    auto packChannels = [&](const int64_t values[4], uint32_t shift) -> uint32_t {
        uint32_t argb = 0u;
        for (uint32_t c = 0; c < 4u; ++c)
            argb |= VivanteBlitFormatOps::Clamp8(roundShiftSigned(values[c], shift)) << ((3u - c) * 8u);
        return argb;
    };

    uint32_t active_taps = 9u;
    if (mode == 2u) {
        const uint32_t configured = (mem_.StateReg(kVrConfigEx) >> 4) & 0xFu;
        if (configured == 0u || configured > 9u)
            mem_.HaltUnsupported("imx6-vivante VR FILTER_TAP encoding is not assigned", kVrConfigEx, configured);
        active_taps = configured;
    }
    const uint32_t first_tap = (9u - active_taps) / 2u;
    const uint32_t last_tap = first_tap + active_taps;
    const uint32_t horizontal_kernel = (mode == 2u) ? kHorizontalKernel : kSharedKernel;
    const uint32_t vertical_kernel = (mode == 2u) ? kVerticalKernel : kSharedKernel;
    const uint32_t alpha_control = mem_.StateReg(kD2dAlphaControl);
    const bool alpha_enable = (alpha_control & 1u) != 0u;
    const uint32_t alpha_modes = mem_.StateReg(kD2dAlphaModes);
    if (!IsModelledAlphaModes(alpha_modes))
        mem_.HaltUnsupported("imx6-vivante VR global alpha or blend mode is not modelled", kD2dAlphaModes,
                             alpha_modes);
    const uint32_t color_multiply_modes = mem_.StateReg(kD2dColorMultiplyModes);
    if (!IsValidColorMultiplyModes(color_multiply_modes))
        mem_.HaltUnsupported("imx6-vivante VR global premultiply encoding is not assigned", kD2dColorMultiplyModes,
                             color_multiply_modes);
    const uint32_t global_src = mem_.StateReg(kD2dGlobalSrcColor);
    const uint32_t global_dst = mem_.StateReg(kD2dGlobalDstColor);

    for (uint32_t dy = dst_top; dy < dst_bottom; ++dy) {
        const int64_t fy = origin_y + static_cast<int64_t>(dy - dst_top) * step_y;
        for (uint32_t dx = dst_left; dx < dst_right; ++dx) {
            const int64_t fx = origin_x + static_cast<int64_t>(dx - dst_left) * step_x;
            uint32_t filtered = 0u;

            if (mode == 0u) {
                int16_t kx[9]{};
                int32_t base_x = 0;
                loadKernel(horizontal_kernel, fx, base_x, kx);
                int64_t sums[4]{};
                const int32_t sample_y = floorFixed16(fy);
                for (uint32_t tx = first_tap; tx < last_tap; ++tx) {
                    uint32_t pixel = 0u;
                    readSourcePixel(static_cast<int64_t>(base_x) + tx - 4, sample_y, pixel);
                    for (uint32_t c = 0; c < 4u; ++c)
                        sums[c] += static_cast<int64_t>(channel(pixel, c)) * kx[tx];
                }
                filtered = packChannels(sums, 14u);
            } else if (mode == 1u) {
                int16_t ky[9]{};
                int32_t base_y = 0;
                loadKernel(vertical_kernel, fy, base_y, ky);
                int64_t sums[4]{};
                const int32_t sample_x = floorFixed16(fx);
                for (uint32_t ty = first_tap; ty < last_tap; ++ty) {
                    uint32_t pixel = 0u;
                    readSourcePixel(sample_x, static_cast<int64_t>(base_y) + ty - 4, pixel);
                    for (uint32_t c = 0; c < 4u; ++c)
                        sums[c] += static_cast<int64_t>(channel(pixel, c)) * ky[ty];
                }
                filtered = packChannels(sums, 14u);
            } else {
                int16_t kx[9]{}, ky[9]{};
                int32_t base_x = 0;
                int32_t base_y = 0;
                loadKernel(horizontal_kernel, fx, base_x, kx);
                loadKernel(vertical_kernel, fy, base_y, ky);
                int64_t sums[4]{};
                for (uint32_t ty = first_tap; ty < last_tap; ++ty) {
                    int64_t horizontal[4]{};
                    for (uint32_t tx = first_tap; tx < last_tap; ++tx) {
                        uint32_t pixel = 0u;
                        readSourcePixel(static_cast<int64_t>(base_x) + tx - 4,
                                        static_cast<int64_t>(base_y) + ty - 4, pixel);
                        for (uint32_t c = 0; c < 4u; ++c)
                            horizontal[c] += static_cast<int64_t>(channel(pixel, c)) * kx[tx];
                    }
                    for (uint32_t c = 0; c < 4u; ++c)
                        sums[c] += horizontal[c] * ky[ty];
                }
                filtered = packChannels(sums, 28u);
            }

            if (alpha_enable) {
                uint32_t dst_argb = 0u;
                readDestPixel(dx, dy, dst_argb);
                filtered = BlendPePixel(mem_, filtered, dst_argb, alpha_modes, kD2dAlphaModes,
                                        color_multiply_modes,
                                        global_src, global_dst);
            }
            writeDestPixel(dx, dy, filtered);
        }
    }
}

}

}
