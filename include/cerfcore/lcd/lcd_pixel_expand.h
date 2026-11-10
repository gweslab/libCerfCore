#pragma once

#include <cstdint>

namespace cerfcore {

namespace lcd_pixel {

inline uint32_t Expand4(uint32_t v) { return (v << 4) | v; }
inline uint32_t Expand5(uint32_t v) { return (v << 3) | (v >> 2); }
inline uint32_t Expand6(uint32_t v) { return (v << 2) | (v >> 4); }

inline uint32_t PackXrgb(uint32_t r, uint32_t g, uint32_t b) {
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

inline uint32_t Luma8(uint32_t xrgb) {
    return (((xrgb >> 16) & 0xFFu) * 77u + ((xrgb >> 8) & 0xFFu) * 150u +
            (xrgb & 0xFFu) * 29u) >> 8;
}

inline uint32_t Expand565(uint16_t px) {
    return PackXrgb(Expand5((px >> 11) & 0x1Fu), Expand6((px >> 5) & 0x3Fu), Expand5(px & 0x1Fu));
}

inline uint32_t Expand555(uint16_t px) {
    return PackXrgb(Expand5((px >> 10) & 0x1Fu), Expand5((px >> 5) & 0x1Fu), Expand5(px & 0x1Fu));
}

inline uint16_t PackRgb565(uint32_t xrgb) {
    return static_cast<uint16_t>((((xrgb >> 19) & 0x1Fu) << 11) |
                                 (((xrgb >> 10) & 0x3Fu) << 5) |
                                 ((xrgb >> 3) & 0x1Fu));
}

inline uint32_t MsbFirstShift(uint32_t bit, uint32_t bpp) { return 8u - bpp - (bit & 7u); }

inline uint32_t PackedIndexMsbFirst(const uint8_t* row, uint32_t x, uint32_t bpp) {
    const uint32_t bit = x * bpp;
    return (row[bit >> 3] >> MsbFirstShift(bit, bpp)) & ((1u << bpp) - 1u);
}

inline uint32_t SubPixelPanelWidth(uint32_t dots) { return (dots + 2u) / 3u; }

template <class DotLevel>
inline uint32_t SubPixelPanelPixel(uint32_t x, uint32_t dots, DotLevel level) {
    const auto dot = [&](uint32_t d) -> uint32_t { return d < dots ? level(d) : 0u; };
    return PackXrgb(dot(3u * x), dot(3u * x + 1u), dot(3u * x + 2u));
}

}

}
