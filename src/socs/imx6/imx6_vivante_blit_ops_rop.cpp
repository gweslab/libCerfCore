#include "imx6_vivante_blit_ops.h"

namespace cerfcore {

namespace imx6_vivante {

namespace {

constexpr uint32_t kMonoMsbFirstBit = 7u; /* In 1-bit-per-pixel data, the first pixel is the most significant bit. */

}

bool VivanteBlitRopOps::RopTruthTableDependsOn(uint32_t code, uint32_t type, uint32_t input_bit) {
    const uint32_t inputs = type == 0u || type == 1u ? 2u : 3u;
    const uint32_t count = 1u << inputs;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t j = i ^ (1u << input_bit);
        if (((code >> i) ^ (code >> j)) & 1u) return true;
    }
    return false;
}

bool VivanteBlitRopOps::RopBranchUsesSource(uint32_t rop, bool foreground) {
    const uint32_t type = (rop >> 20) & 3u;
    const uint32_t code = RopCode(rop, foreground);
    if (type >= 1u)
        return RopTruthTableDependsOn(code, type, 1u);
    return false;
}

bool VivanteBlitRopOps::RopBranchUsesPattern(uint32_t rop, bool foreground) {
    const uint32_t type = (rop >> 20) & 3u;
    const uint32_t code = RopCode(rop, foreground);
    if (type == 0u)
        return RopTruthTableDependsOn(code, type, 1u);
    if (type >= 2u)
        return RopTruthTableDependsOn(code, type, 2u);
    return false;
}

bool VivanteBlitRopOps::RopBranchUsesDestination(uint32_t rop, bool foreground) {
    const uint32_t type = (rop >> 20) & 3u;
    return RopTruthTableDependsOn(RopCode(rop, foreground), type, 0u);
}

bool VivanteBlitRopOps::RopUsesSource(uint32_t rop) {
    const uint32_t type = (rop >> 20) & 3u;
    return RopBranchUsesSource(rop, true) || (type == 3u && RopBranchUsesSource(rop, false));
}

bool VivanteBlitRopOps::RopUsesDestination(uint32_t rop) {
    const uint32_t type = (rop >> 20) & 3u;
    return RopBranchUsesDestination(rop, true) || (type == 3u && RopBranchUsesDestination(rop, false));
}

VivanteBlitRopOps::MonoRopSelection VivanteBlitRopOps::SelectMonoRop(bool mono_one, bool mono_transparency_enabled,
                                                                     bool mono_transparent_one) {
    if (mono_transparency_enabled) {
        const bool transparent = mono_one == mono_transparent_one;
        return {!transparent};
    }
    return {mono_one};
}

uint32_t VivanteBlitRopOps::ApplyRop(uint32_t rop, uint32_t dst, uint32_t src, uint32_t pat, bool foreground) {
    const uint32_t type = (rop >> 20) & 3u;
    const uint32_t code = RopCode(rop, foreground);

    uint32_t out = 0u;
    for (uint32_t bit = 0; bit < 32u; ++bit) {
        const uint32_t d = (dst >> bit) & 1u;
        const uint32_t s = (src >> bit) & 1u;
        const uint32_t p = (pat >> bit) & 1u;
        uint32_t idx = 0u;
        if (type == 0u)
            idx = d | (p << 1);
        else if (type == 1u)
            idx = d | (s << 1);
        else
            idx = d | (s << 1) | (p << 2);
        if ((code >> idx) & 1u) out |= 1u << bit;
    }
    return out;
}

bool VivanteBlitRopOps::ReadStreamMonoBit(const uint8_t* stream, uint32_t x, uint32_t y, uint32_t width,
                                          uint32_t height, uint32_t pack, uint32_t stream_words, uint32_t& bit) {
    if (x >= width || y >= height) return false;
    uint32_t pixels_per_chunk = 0u;
    uint32_t rows_per_chunk = 0u;
    switch (pack) {
    case 0u: pixels_per_chunk = 8u; rows_per_chunk = 4u; break;
    case 1u: pixels_per_chunk = 16u; rows_per_chunk = 2u; break;
    case 2u: pixels_per_chunk = 32u; rows_per_chunk = 1u; break;
    case 3u: {
        const uint64_t byte_index = static_cast<uint64_t>(y) * (((static_cast<uint64_t>(width) + 31u) & ~uint64_t{31u}) >> 3) + (x >> 3);
        if (byte_index >= static_cast<uint64_t>(stream_words) * 4u) return false;
        bit = (stream[byte_index] >> (kMonoMsbFirstBit - (x & 7u))) & 1u;
        return true;
    }
    default: return false;
    }
    const uint64_t aligned_height = (static_cast<uint64_t>(height) + rows_per_chunk - 1u) & ~(static_cast<uint64_t>(rows_per_chunk) - 1u);
    const uint64_t bits_per_x_chunk = static_cast<uint64_t>(pixels_per_chunk) * aligned_height;
    const uint64_t row_bit = static_cast<uint64_t>(y) * pixels_per_chunk + (x % pixels_per_chunk);
    const uint64_t x_chunk = x / pixels_per_chunk;
    if (x_chunk != 0u && bits_per_x_chunk > (UINT64_MAX - row_bit) / x_chunk) return false;
    const uint64_t byte_index = (x_chunk * bits_per_x_chunk + row_bit) >> 3;
    if (byte_index >= static_cast<uint64_t>(stream_words) * 4u) return false;
    bit = (stream[byte_index] >> (kMonoMsbFirstBit - (x & 7u))) & 1u;
    return true;
}

}

}
