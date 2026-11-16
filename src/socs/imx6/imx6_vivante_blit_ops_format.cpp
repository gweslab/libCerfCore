#include "imx6_vivante_blit_ops.h"
#include "imx6_vivante_mem.h"

namespace cerfcore {

namespace imx6_vivante {

namespace {

uint16_t PackA1R5G5B5(uint32_t argb, uint32_t swizzle) {
    const bool swap_red_blue = (swizzle & 3u) == 3u;
    const uint32_t r = (argb >> (swap_red_blue ? 0u : 16u)) & 0xFFu;
    const uint32_t g = (argb >> 8) & 0xFFu;
    const uint32_t b = (argb >> (swap_red_blue ? 16u : 0u)) & 0xFFu;
    const uint32_t a = (argb >> 31) & 1u;
    return static_cast<uint16_t>((a << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

}

uint32_t VivanteBlitFormatOps::BppFromDeFormat(uint32_t fmt) {
    switch (fmt & 0x1Fu) {
    case 0: return 2u;
    case 1: return 2u;
    case 2: return 2u;
    case 3: return 2u;
    case 4: return 2u;
    case 5: return 4u;
    case 6: return 4u;
    case 9: return 1u;
    case 16: return 1u;
    default: return 0u;
    }
}

uint32_t VivanteBlitFormatOps::PatternBytesPerPixel(VivanteMem& mem, uint32_t config) {
    uint32_t bpp = BppFromDeFormat((config >> 24) & 0x1Fu);
    const uint32_t selector = (config >> 30) & 3u;
    if (selector == 1u)
        bpp = 2u;
    else if (selector == 2u)
        bpp = 4u;
    else if (selector == 3u)
        mem.HaltUnsupported("imx6-vivante PATTERN_CONFIG has both bpp selectors set", 0x123Cu, config);
    return bpp;
}

uint32_t VivanteBlitFormatOps::YuvToArgb(uint32_t y, uint32_t u, uint32_t v, bool bt709) {
    const int32_t a = static_cast<int32_t>(y) - 16;
    const int32_t b = static_cast<int32_t>(u) - 128;
    const int32_t c = static_cast<int32_t>(v) - 128;
    const int32_t r = bt709 ? ((298 * a + 461 * c + 128) >> 8) : ((298 * a + 410 * c + 128) >> 8);
    const int32_t g = bt709 ? ((298 * a - 55 * b - 137 * c + 128) >> 8) : ((298 * a - 101 * b - 209 * c + 128) >> 8);
    const int32_t blue = bt709 ? ((298 * a + 543 * b + 128) >> 8) : ((298 * a + 519 * b + 128) >> 8);
    return 0xFF000000u | (Clamp8(r) << 16) | (Clamp8(g) << 8) | Clamp8(blue);
}

VivanteBlitFormatOps::ArgbChannels VivanteBlitFormatOps::BlendFactor(VivanteMem& mem, uint32_t mode,
                                                                     uint32_t alpha_modes_register,
                                                                     const ArgbChannels& reference,
                                                                     uint32_t source_alpha, uint32_t destination_alpha) {
    ArgbChannels f{};
    switch (mode & 7u) {
    case 0u:  return f;
    case 1u:  return {255u, 255u, 255u, 255u};
    case 2u:  return {reference.a, reference.a, reference.a, reference.a};
    case 3u:  return {255u - reference.a, 255u - reference.a, 255u - reference.a, 255u - reference.a};
    case 4u:  return reference;
    case 5u:
        return {255u - reference.a, 255u - reference.r, 255u - reference.g, 255u - reference.b};
    case 6u: {
        const uint32_t sat = source_alpha < (255u - destination_alpha) ? source_alpha : (255u - destination_alpha);
        return {255u, sat, sat, sat};
    }
    case 7u:
        mem.HaltUnsupported("imx6-vivante GC320 blend-factor mode 7 SATURATED_DEST_ALPHA is not defined by the GC320 TRM",
                            alpha_modes_register, mode);
    }
    return f;
}

uint32_t VivanteBlitFormatOps::BlendPePixel(VivanteMem& mem, uint32_t src_argb, uint32_t dst_argb, uint32_t alpha_modes,
                                            uint32_t alpha_modes_register,
                                            uint32_t color_multiply_modes, uint32_t global_src_color,
                                             uint32_t global_dst_color) {
    ArgbChannels src = SplitArgb(src_argb);
    ArgbChannels dst = SplitArgb(dst_argb);
    const ArgbChannels global_src = SplitArgb(global_src_color);
    const ArgbChannels global_dst = SplitArgb(global_dst_color);

    const uint32_t global_src_alpha = global_src.a;
    const uint32_t global_dst_alpha = global_dst.a;
    src.a = EffectiveAlpha(src.a, global_src_alpha, (alpha_modes & 1u) != 0u, (alpha_modes >> 8) & 3u);
    dst.a = EffectiveAlpha(dst.a, global_dst_alpha, (alpha_modes & (1u << 4)) != 0u, (alpha_modes >> 12) & 3u);

    const bool src_premultiply = (color_multiply_modes & 1u) != 0u;
    const bool dst_premultiply = (color_multiply_modes & (1u << 4)) != 0u;
    if (src_premultiply) {
        src.r = Scale8(src.r, src.a);
        src.g = Scale8(src.g, src.a);
        src.b = Scale8(src.b, src.a);
    }
    if (dst_premultiply) {
        dst.r = Scale8(dst.r, dst.a);
        dst.g = Scale8(dst.g, dst.a);
        dst.b = Scale8(dst.b, dst.a);
    }

    const uint32_t global_premultiply = (color_multiply_modes >> 8) & 3u;
    if (global_premultiply == 1u) {
        src.r = Scale8(src.r, global_src.a);
        src.g = Scale8(src.g, global_src.a);
        src.b = Scale8(src.b, global_src.a);
    } else if (global_premultiply == 2u) {
        src.r = Scale8(src.r, global_src.r);
        src.g = Scale8(src.g, global_src.g);
        src.b = Scale8(src.b, global_src.b);
    }

    ArgbChannels src_reference = (alpha_modes & (1u << 27)) ? src : dst;
    ArgbChannels dst_reference = (alpha_modes & (1u << 31)) ? dst : src;
    const ArgbChannels src_factor =
        BlendFactor(mem, (alpha_modes >> 24) & 7u, alpha_modes_register, src_reference, src.a, dst.a);
    const ArgbChannels dst_factor =
        BlendFactor(mem, (alpha_modes >> 28) & 7u, alpha_modes_register, dst_reference, src.a, dst.a);

    auto add = [](uint32_t source, uint32_t sf, uint32_t destination, uint32_t df) -> uint32_t {
        const uint32_t value = source * sf + destination * df;
        const uint32_t rounded = (value + 127u) / 255u;
        return rounded > 255u ? 255u : rounded;
    };
    ArgbChannels out{add(src.a, src_factor.a, dst.a, dst_factor.a), add(src.r, src_factor.r, dst.r, dst_factor.r),
                     add(src.g, src_factor.g, dst.g, dst_factor.g), add(src.b, src_factor.b, dst.b, dst_factor.b)};

    if ((color_multiply_modes & (1u << 20)) != 0u && out.a != 0u) {
        auto demultiply = [&](uint32_t channel) -> uint32_t {
            const uint32_t value = (channel * 255u + out.a / 2u) / out.a;
            return value > 255u ? 255u : value;
        };
        out.r = demultiply(out.r);
        out.g = demultiply(out.g);
        out.b = demultiply(out.b);
    }
    return JoinArgb(out);
}

bool VivanteBlitFormatOps::NativeColorKeyMatch(uint32_t packed, uint32_t low, uint32_t high, uint32_t fmt,
                                               uint32_t swizzle) {
    struct Components {
        uint32_t value[4]{};
        uint32_t count = 0u;
    };
    auto split = [swizzle](uint32_t value, uint32_t format, bool color_key) -> Components {
        Components c{};
        switch (format & 0x1Fu) {
        case 0u:
            c.value[0] = (value >> 8) & 0xFu;
            c.value[1] = (value >> 4) & 0xFu;
            c.value[2] = value & 0xFu;
            c.count = 3u;
            break;
        case 1u:
            c.value[0] = (value >> 12) & 0xFu;
            c.value[1] = (value >> 8) & 0xFu;
            c.value[2] = (value >> 4) & 0xFu;
            c.value[3] = value & 0xFu;
            c.count = 4u;
            break;
        case 2u:
            c.value[0] = (value >> 10) & 0x1Fu;
            c.value[1] = (value >> 5) & 0x1Fu;
            c.value[2] = value & 0x1Fu;
            c.count = 3u;
            break;
        case 3u:
            if ((swizzle & 3u) == 1u || (swizzle & 3u) == 3u) {
                c.value[0] = value & 1u;
                c.value[1] = (value >> 11) & 0x1Fu;
                c.value[2] = (value >> 6) & 0x1Fu;
                c.value[3] = (value >> 1) & 0x1Fu;
            } else {
                c.value[0] = (value >> 15) & 1u;
                c.value[1] = (value >> 10) & 0x1Fu;
                c.value[2] = (value >> 5) & 0x1Fu;
                c.value[3] = value & 0x1Fu;
            }
            c.count = 4u;
            break;
        case 4u:
            c.value[0] = (value >> 11) & 0x1Fu;
            c.value[1] = (value >> 5) & 0x3Fu;
            c.value[2] = value & 0x1Fu;
            c.count = 3u;
            break;
        case 5u:
            c.value[0] = (value >> 16) & 0xFFu;
            c.value[1] = (value >> 8) & 0xFFu;
            c.value[2] = value & 0xFFu;
            c.count = 3u;
            break;
        case 6u:
            c.value[0] = (value >> 24) & 0xFFu;
            c.value[1] = (value >> 16) & 0xFFu;
            c.value[2] = (value >> 8) & 0xFFu;
            c.value[3] = value & 0xFFu;
            c.count = 4u;
            break;
        case 9u:
        case 16u:
            c.value[0] = color_key ? ((value >> 24) & 0xFFu) : (value & 0xFFu);
            c.count = 1u;
            break;
        default:
            c.value[0] = value;
            c.count = 1u;
            break;
        }
        return c;
    };

    const Components pixel = split(packed, fmt, false);
    const Components lo = split(low, fmt, true);
    const Components hi = split(high, fmt, true);
    for (uint32_t i = 0u; i < pixel.count; ++i) {
        if (pixel.value[i] < lo.value[i] || pixel.value[i] > hi.value[i]) return false;
    }
    return true;
}

uint32_t VivanteBlitFormatOps::UnpackSurfaceColor(uint32_t packed, uint32_t fmt, uint32_t swizzle) {
    switch (fmt & 0x1Fu) {
    case 0: return ApplyReadSwizzle(Rgba4444ToArgb(static_cast<uint16_t>(packed), false), swizzle) | 0xFF000000u;
    case 1: return ApplyReadSwizzle(Rgba4444ToArgb(static_cast<uint16_t>(packed), true), swizzle);
    case 2: return ApplyReadSwizzle(Rgb555ToArgb(static_cast<uint16_t>(packed), false), swizzle) | 0xFF000000u;
    case 3:
        if ((swizzle & 3u) == 1u || (swizzle & 3u) == 3u) {
            const uint16_t px = static_cast<uint16_t>(packed);
            const uint32_t r5 = (px >> 11) & 0x1Fu;
            const uint32_t g5 = (px >> 6) & 0x1Fu;
            const uint32_t b5 = (px >> 1) & 0x1Fu;
            const uint32_t r = ((swizzle & 3u) == 1u ? r5 : b5);
            const uint32_t b = ((swizzle & 3u) == 1u ? b5 : r5);
            const uint32_t a = (px & 1u) ? 0xFFu : 0u;
            return (a << 24) | (((r << 3) | (r >> 2)) << 16) | (((g5 << 3) | (g5 >> 2)) << 8) |
                   ((b << 3) | (b >> 2));
        }
        return ApplyReadSwizzle(Rgb555ToArgb(static_cast<uint16_t>(packed), true), swizzle);
    case 4: return ApplyReadSwizzle(Rgb565ToArgb(static_cast<uint16_t>(packed)), swizzle);
    case 5: return ApplyReadSwizzle(packed, swizzle) | 0xFF000000u;
    case 6: return ApplyReadSwizzle(packed, swizzle);
    case 16: return ((packed & 0xFFu) << 24) | 0x00FFFFFFu;
    default: return packed;
    }
}

uint32_t VivanteBlitFormatOps::PackSurfaceColor(uint32_t argb, uint32_t fmt, uint32_t swizzle) {
    switch (fmt & 0x1Fu) {
    case 0: return ArgbToRgba4444(ApplyWriteSwizzle(argb, swizzle), false);
    case 1: return ArgbToRgba4444(ApplyWriteSwizzle(argb, swizzle), true);
    case 2: return ArgbToRgb555(ApplyWriteSwizzle(argb, swizzle), false);
    case 3:
        if ((swizzle & 3u) == 1u || (swizzle & 3u) == 3u) {
            return PackA1R5G5B5(argb, swizzle);
        }
        return ArgbToRgb555(ApplyWriteSwizzle(argb, swizzle), true);
    case 4: return ArgbToRgb565(ApplyWriteSwizzle(argb, swizzle));
    case 5: return ApplyWriteSwizzle(argb, swizzle);
    case 6: return ApplyWriteSwizzle(argb, swizzle);
    case 16: return (argb >> 24) & 0xFFu;
    default: return argb;
    }
}

uint32_t VivanteBlitFormatOps::DecodeSourcePixel(uint32_t packed, uint32_t fmt, uint32_t swizzle,
                                                 uint32_t global_src_color, uint32_t index_color) {
    switch (fmt & 0x1Fu) {
    case 9u: return index_color;
    case 16u: return ((packed & 0xFFu) << 24) | (global_src_color & 0x00FFFFFFu);
    default: return UnpackSurfaceColor(packed, fmt, swizzle);
    }
}

}

}
