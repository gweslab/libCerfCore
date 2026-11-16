#pragma once

#include <cstdint>

namespace cerfcore {

namespace imx6_vivante {

class VivanteMem;

struct DeRotation {
    uint32_t mode;
    uint32_t mirror;
};

struct VivanteBlitCoordinateOps {
    static uint32_t Lo16(uint32_t v) { return v & 0xFFFFu; }
    static uint32_t Hi16(uint32_t v) { return (v >> 16) & 0xFFFFu; }

    static bool ResolveSourceCoordinate(uint32_t origin_field, bool relative, uint32_t destination_coordinate,
                                        uint32_t rectangle_offset, uint32_t& source_coordinate);
    static bool ResolveRopResource(bool dependency, uint32_t override_mode) {
        return override_mode == 1u ? true : override_mode == 2u ? false : dependency;
    }

    static DeRotation DecodeDeRotation(uint32_t angle, uint32_t config, bool source, bool angle_supported) {
        const uint32_t rotation_shift = source ? 0u : 3u;
        const uint32_t mirror_shift = source ? 12u : 16u;
        const uint32_t mode = angle_supported ? ((angle >> rotation_shift) & 7u)
                                              : (((config >> 16) & 1u) ? 4u : 0u);
        const uint32_t mirror = angle_supported ? ((angle >> mirror_shift) & 3u) : 0u;
        return {mode, mirror};
    }

    static uint32_t MonoStreamCoordinate(uint32_t origin_field, bool relative, uint32_t rectangle_offset);
    static void ResolveMonoStreamExtent(uint32_t origin_field, bool relative, uint32_t rectangle_extent,
                                        uint32_t& stream_extent);
    static uint32_t StretchCoordinate(uint32_t destination_offset, uint32_t programmed_factor, bool gdi_stretch);
};

struct VivanteBlitFormatOps {
    static uint32_t BppFromDeFormat(uint32_t fmt);
    static uint32_t PatternBytesPerPixel(VivanteMem& mem, uint32_t config);

    static bool IsKnownDeFormat(uint32_t fmt) {
        switch (fmt & 0x1Fu) {
        case 0u:
        case 1u:
        case 2u:
        case 3u:
        case 4u:
        case 5u:
        case 6u:
        case 7u:
        case 8u:
        case 9u:
        case 10u:
        case 15u:
        case 16u:
        case 17u:
        case 18u:
        case 19u: return true;
        default: return false;
        }
    }

    static bool IsVrYuvFormat(uint32_t fmt) {
        switch (fmt & 0x1Fu) {
        case 7u:
        case 8u:
        case 15u:
        case 17u:
        case 18u:  return true;
        default: return false;
        }
    }

    static uint32_t YuvToArgb(uint32_t y, uint32_t u, uint32_t v, bool bt709);

    static uint32_t Clamp8(int32_t value) {
        if (value < 0) return 0u;
        if (value > 255) return 255u;
        return static_cast<uint32_t>(value);
    }

    static uint32_t Rgb565ToArgb(uint16_t px) {
        const uint32_t r5 = (px >> 11) & 0x1Fu;
        const uint32_t g6 = (px >> 5) & 0x3Fu;
        const uint32_t b5 = px & 0x1Fu;
        const uint32_t r = (r5 << 3) | (r5 >> 2);
        const uint32_t g = (g6 << 2) | (g6 >> 4);
        const uint32_t b = (b5 << 3) | (b5 >> 2);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    static uint16_t ArgbToRgb565(uint32_t argb) {
        const uint32_t r = (argb >> 16) & 0xFFu;
        const uint32_t g = (argb >> 8) & 0xFFu;
        const uint32_t b = argb & 0xFFu;
        return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    static uint32_t Rgba4444ToArgb(uint16_t px, bool alpha) {
        const uint32_t a4 = alpha ? ((px >> 12) & 0xFu) : 0xFu;
        const uint32_t r4 = (px >> 8) & 0xFu;
        const uint32_t g4 = (px >> 4) & 0xFu;
        const uint32_t b4 = px & 0xFu;
        return ((a4 * 0x11u) << 24) | ((r4 * 0x11u) << 16) | ((g4 * 0x11u) << 8) | (b4 * 0x11u);
    }

    static uint16_t ArgbToRgba4444(uint32_t argb, bool alpha) {
        const uint32_t a = alpha ? (((argb >> 28) & 0xFu) << 12) : 0u;
        const uint32_t r = ((argb >> 20) & 0xFu) << 8;
        const uint32_t g = ((argb >> 12) & 0xFu) << 4;
        const uint32_t b = (argb >> 4) & 0xFu;
        return static_cast<uint16_t>(a | r | g | b);
    }

    static uint32_t Rgb555ToArgb(uint16_t px, bool alpha) {
        const uint32_t a = alpha ? ((px & 0x8000u) ? 0xFF000000u : 0u) : 0xFF000000u;
        const uint32_t r5 = (px >> 10) & 0x1Fu;
        const uint32_t g5 = (px >> 5) & 0x1Fu;
        const uint32_t b5 = px & 0x1Fu;
        const uint32_t r = (r5 << 3) | (r5 >> 2);
        const uint32_t g = (g5 << 3) | (g5 >> 2);
        const uint32_t b = (b5 << 3) | (b5 >> 2);
        return a | (r << 16) | (g << 8) | b;
    }

    static uint16_t ArgbToRgb555(uint32_t argb, bool alpha) {
        const uint32_t a = alpha ? (((argb >> 31) & 1u) << 15) : 0u;
        const uint32_t r = ((argb >> 19) & 0x1Fu) << 10;
        const uint32_t g = ((argb >> 11) & 0x1Fu) << 5;
        const uint32_t b = (argb >> 3) & 0x1Fu;
        return static_cast<uint16_t>(a | r | g | b);
    }

    static uint32_t Scale8(uint32_t a, uint32_t b) { return (a * b + 127u) / 255u; }

    struct ArgbChannels {
        uint32_t a = 0u;
        uint32_t r = 0u;
        uint32_t g = 0u;
        uint32_t b = 0u;
    };

    static ArgbChannels SplitArgb(uint32_t argb) {
        return {(argb >> 24) & 0xFFu, (argb >> 16) & 0xFFu, (argb >> 8) & 0xFFu, argb & 0xFFu};
    }

    static uint32_t JoinArgb(const ArgbChannels& c) {
        return ((c.a & 0xFFu) << 24) | ((c.r & 0xFFu) << 16) | ((c.g & 0xFFu) << 8) | (c.b & 0xFFu);
    }

    static bool IsModelledAlphaModes(uint32_t alpha_modes) {
        return ((alpha_modes >> 8) & 3u) != 3u && ((alpha_modes >> 12) & 3u) != 3u;
    }

    static uint32_t EffectiveAlpha(uint32_t pixel_alpha, uint32_t global_alpha, bool inverse, uint32_t global_mode) {
        const uint32_t alpha = inverse ? (255u - pixel_alpha) : pixel_alpha;
        if ((global_mode & 3u) == 1u) return global_alpha;
        if ((global_mode & 3u) == 2u) return Scale8(alpha, global_alpha);
        return alpha;
    }

    static ArgbChannels BlendFactor(VivanteMem& mem, uint32_t mode, uint32_t alpha_modes_register,
                                    const ArgbChannels& reference, uint32_t source_alpha,
                                    uint32_t destination_alpha);
    static uint32_t BlendPePixel(VivanteMem& mem, uint32_t src_argb, uint32_t dst_argb, uint32_t alpha_modes,
                                 uint32_t alpha_modes_register,
                                 uint32_t color_multiply_modes, uint32_t global_src_color, uint32_t global_dst_color);
    static bool NativeColorKeyMatch(uint32_t packed, uint32_t low, uint32_t high, uint32_t fmt,
                                    uint32_t swizzle = 0u);
    static bool SourceTransparent(uint32_t kind, uint32_t packed, uint32_t low, uint32_t high, uint32_t fmt,
                                  uint32_t swizzle) {
        return kind == 2u && NativeColorKeyMatch(packed, low, high, fmt, swizzle);
    }

    static bool IsValidEndian(uint32_t endian) { return (endian & 3u) != 3u; }

    struct DeSourceConfig {
        uint32_t format;
        uint32_t swizzle;
        uint32_t endian;
        bool relative;
        bool tiled;
        bool multi_tiled;
        bool supertiled;
        bool minor_tiled;
    };

    static DeSourceConfig DecodeDeSourceConfig(uint32_t config, uint32_t extended) {
        return {(config >> 24) & 0x1Fu,
                (config >> 20) & 3u,
                (config >> 30) & 3u,
                (config & (1u << 6)) != 0u,
                (config & (1u << 7)) != 0u,
                (extended & 1u) != 0u,
                (extended & (1u << 3)) != 0u,
                (extended & (1u << 8)) != 0u};
    }

    struct DeDestinationConfig {
        uint32_t format;
        uint32_t swizzle;
        uint32_t endian;
        bool tiled;
        bool minor_tiled;
    };

    static DeDestinationConfig DecodeDeDestinationConfig(uint32_t config) {
        return {config & 0x1Fu, (config >> 16) & 3u, (config >> 20) & 3u, (config & (1u << 8)) != 0u,
                (config & (1u << 26)) != 0u};
    }

    struct PeTransparencyConfig {
        uint32_t source;
        uint32_t pattern;
        uint32_t destination;
        uint32_t source_override;
        uint32_t pattern_override;
        uint32_t destination_override;
    };

    static PeTransparencyConfig DecodePeTransparency(uint32_t value) {
        return {value & 3u, (value >> 4) & 3u, (value >> 8) & 3u,
                (value >> 16) & 3u, (value >> 20) & 3u, (value >> 24) & 3u};
    }

    static bool IsValidColorMultiplyModes(uint32_t modes) { return ((modes >> 8) & 3u) != 3u; }

    static bool IsValidPeTransparency(uint32_t value) {
        const uint32_t pattern = (value >> 4) & 3u;
        const uint32_t destination = (value >> 8) & 3u;
        if ((value & 3u) == 3u || pattern == 3u || pattern == 2u) return false;
        if (destination == 3u || destination == 1u) return false;
        return ((value >> 16) & 3u) != 3u && ((value >> 20) & 3u) != 3u && ((value >> 24) & 3u) != 3u;
    }

    static uint64_t EndianByteOffset(uint64_t offset, uint32_t endian) {
        const uint64_t word = offset & ~static_cast<uint64_t>(3u);
        uint64_t lane = offset & 3u;
        if ((endian & 3u) == 1u) lane ^= 1u;
        if ((endian & 3u) == 2u) lane ^= 3u;
        return word + lane;
    }

    template <bool kWrite>
    static uint32_t ApplySwizzle(uint32_t argb, uint32_t swizzle) {
        const uint32_t a = (argb >> 24) & 0xFFu;
        const uint32_t r = (argb >> 16) & 0xFFu;
        const uint32_t g = (argb >> 8) & 0xFFu;
        const uint32_t b = argb & 0xFFu;
        switch (swizzle & 3u) {
        case 1:
            if constexpr (kWrite) {
                return (r << 24) | (g << 16) | (b << 8) | a;
            } else {
                return (b << 24) | (a << 16) | (r << 8) | g;
            }
        case 2: return (a << 24) | (b << 16) | (g << 8) | r;
        case 3: return (b << 24) | (g << 16) | (r << 8) | a;
        default: return argb;
        }
    }

    static uint32_t ApplyReadSwizzle(uint32_t argb, uint32_t swizzle) { return ApplySwizzle<false>(argb, swizzle); }
    static uint32_t ApplyWriteSwizzle(uint32_t argb, uint32_t swizzle) { return ApplySwizzle<true>(argb, swizzle); }

    static uint32_t UnpackSurfaceColor(uint32_t packed, uint32_t fmt, uint32_t swizzle = 0u);
    static uint32_t PackSurfaceColor(uint32_t argb, uint32_t fmt, uint32_t swizzle = 0u);
    static uint32_t DecodeSourcePixel(uint32_t packed, uint32_t fmt, uint32_t swizzle,
                                      uint32_t global_src_color, uint32_t index_color);
};

struct VivanteBlitSurfaceOps {
    enum class SurfaceLayout : uint8_t {
        Linear = 0,
        Tiled,
        SuperTiled,
        MultiTiled,
        MultiSuperTiled,
    };

    struct SurfaceLocation {
        uint32_t plane = 0u;
        uint64_t offset = 0u;
        bool valid = false;
    };

    static bool IsMultiLayout(SurfaceLayout layout) {
        return layout == SurfaceLayout::MultiTiled || layout == SurfaceLayout::MultiSuperTiled;
    }

    static uint32_t SurfaceWidthFromStride(uint32_t stride, uint32_t bpp, SurfaceLayout layout);
    static SurfaceLayout DecodeSurfaceLayout(bool tiled, bool multi_tiled, bool supertiled);
    static SurfaceLocation LocateSurface(uint32_t stride, uint32_t x, uint32_t y, uint32_t bpp,
                                         SurfaceLayout layout);

    template <typename PixelFn>
    static void RasterizeLineBresenhamExclusive(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                                                PixelFn&& emit_pixel) {
        int32_t x = static_cast<int32_t>(x0);
        int32_t y = static_cast<int32_t>(y0);
        const int32_t end_x = static_cast<int32_t>(x1);
        const int32_t end_y = static_cast<int32_t>(y1);

        if (x == end_x && y == end_y) return;

        const int32_t dx = (end_x >= x) ? (end_x - x) : (x - end_x);
        const int32_t sx = (x < end_x) ? 1 : -1;
        const int32_t abs_dy = (end_y >= y) ? (end_y - y) : (y - end_y);
        const int32_t dy = -abs_dy;
        const int32_t sy = (y < end_y) ? 1 : -1;
        int32_t error = dx + dy;
        while (x != end_x || y != end_y) {
            emit_pixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
            const int32_t twice_error = error * 2;
            if (twice_error >= dy) {
                error += dy;
                x += sx;
            }
            if (twice_error <= dx) {
                error += dx;
                y += sy;
            }
        }
    }

    struct DeCoord {
        uint32_t x = 0;
        uint32_t y = 0;
        bool valid = true;
    };

    static bool IsValidDeRot(uint32_t rot) {
        return rot == 0u || rot == 1u || rot == 2u || rot == 4u || rot == 5u || rot == 6u;
    }

    static DeCoord TransformDeCoord(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t rot,
                                    uint32_t mirror);
};

struct VivanteBlitRopOps {
    static bool IsTransparentPixel(bool destination_opaque, bool source_transparent) {
        return !destination_opaque || source_transparent;
    }

    static uint32_t PatternOriginX(uint32_t config) { return (config >> 16) & 7u; }
    static uint32_t PatternOriginY(uint32_t config) { return (config >> 20) & 7u; }

    static uint32_t RopCode(uint32_t rop, bool foreground = true) {
        const uint32_t type = (rop >> 20) & 3u;
        if (type == 3u) return foreground ? (rop & 0xFFu) : ((rop >> 8) & 0xFFu);
        return rop & 0xFFu;
    }

    static bool RopTruthTableDependsOn(uint32_t code, uint32_t type, uint32_t input_bit);
    static bool RopBranchUsesSource(uint32_t rop, bool foreground);
    static bool RopBranchUsesPattern(uint32_t rop, bool foreground);
    static bool RopBranchUsesDestination(uint32_t rop, bool foreground);
    static bool RopUsesSource(uint32_t rop);
    static bool RopUsesDestination(uint32_t rop);

    struct MonoRopSelection {
        bool foreground = true;
    };

    static MonoRopSelection SelectMonoRop(bool mono_one, bool mono_transparency_enabled, bool mono_transparent_one);

    static bool AddressRangesOverlap(uint64_t a_begin, uint64_t a_end, uint64_t b_begin, uint64_t b_end) {
        return a_begin < b_end && b_begin < a_end;
    }

    static uint32_t ApplyRop(uint32_t rop, uint32_t dst, uint32_t src, uint32_t pat, bool foreground = true);
    static bool ReadStreamMonoBit(const uint8_t* stream, uint32_t x, uint32_t y, uint32_t width,
                                  uint32_t height, uint32_t pack, uint32_t stream_words, uint32_t& bit);


};

struct VivanteBlitOps : VivanteBlitCoordinateOps,
                        VivanteBlitFormatOps,
                        VivanteBlitSurfaceOps,
                        VivanteBlitRopOps {
    static DeCoord TransformIfNeeded(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                     uint32_t rotation, uint32_t mirror) {
        if (rotation == 0u && mirror == 0u) return {x, y};
        return TransformDeCoord(x, y, width, height, rotation, mirror);
    }

};

}

}
