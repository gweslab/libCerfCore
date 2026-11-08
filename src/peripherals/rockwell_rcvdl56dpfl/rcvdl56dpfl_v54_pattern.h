#pragma once

#include <cstdint>

namespace cerfcore {

struct Rcvdl56dpflV54Pattern {
    static constexpr uint8_t kPreparatory = 0x04u;
    static constexpr uint8_t kAcknowledge = 0x08u;

    uint32_t scrambled_bits = 0u;
    uint8_t  input          = 0u;
    uint32_t trailing_ones  = 0u;

    uint32_t Bits() const { return scrambled_bits + trailing_ones; }

    static constexpr Rcvdl56dpflV54Pattern Of(uint8_t pattern_bit) {
        if (pattern_bit == kPreparatory) return {2048u, 0u, 0u};
        if (pattern_bit == kAcknowledge) return {1948u, 1u, 0u};
        return {8192u, 1u, 64u};
    }
};

class Rcvdl56dpflV54Scrambler {
public:
    uint8_t Next(uint8_t input) {
        const uint8_t bit = static_cast<uint8_t>((input ^ (history_ >> 3) ^ (history_ >> 6)) & 1u);
        history_ = static_cast<uint8_t>(((history_ << 1) | bit) & 0x7Fu);
        return bit;
    }

private:
    uint8_t history_ = 0x7Fu;
};

}
