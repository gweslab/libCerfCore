#pragma once
#include "adreno_a2xx_tiling.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <vector>

namespace cerfcore {

struct AdrenoA2xxTextureLevel {
    uint64_t base   = 0;
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t pitch  = 0;
    uint32_t mip_x  = 0;
    uint32_t mip_y  = 0;
};

class AdrenoA2xxTextureLayout {
public:
    explicit AdrenoA2xxTextureLayout(const std::array<uint32_t, 6>& state)
        : state_(state),
          bytes_(FormatBytes(state[1] & 63u)),
          pitch_(((state[0] >> 22) & 511u) * 32u),
          width_((state[2] & 8191u) + 1u),
          height_(((state[2] >> 13) & 8191u) + 1u),
          last_level_(std::bit_width(width_) - 1u),
          tail_level_(last_level_ >= 4u ? last_level_ - 4u : 0u),
          cube_(((state[5] >> 9) & 3u) == 3u) {}

    static uint32_t FormatBytes(uint32_t format) {
        return format == 6u ? 4u : (format == 4u || format == 15u || format == 10u) ? 2u : format == 2u ? 1u : 0u;
    }

    uint32_t Bytes() const { return bytes_; }
    uint32_t LastLevel() const { return last_level_; }
    uint64_t LinearBytes() const { return uint64_t(height_ - 1u) * pitch_ * bytes_ + uint64_t(width_) * bytes_; }

    AdrenoA2xxTextureLevel Level(uint32_t level, uint32_t face) const {
        AdrenoA2xxTextureLevel out;
        out.width  = (std::max)(1u, width_ >> level);
        out.height = (std::max)(1u, height_ >> level);
        out.pitch  = level ? (std::max)(32u, pitch_ >> level) : pitch_;
        out.base   = (level ? state_[5] : state_[1]) & 0xFFFFF000u;
        if (cube_)
            out.base += face * ((uint64_t(pitch_) * ((height_ + 31u) & ~31u) * bytes_ + 4095u) & ~uint64_t{4095u});
        if (level) {
            for (uint32_t preceding = 1; preceding < (std::min)(level, tail_level_); ++preceding) {
                const uint64_t side = (std::max)(32u, width_ >> preceding);
                out.base += (side * side * bytes_ + 4095u) & ~uint64_t{4095u};
            }
            if (level >= tail_level_) {
                const uint32_t relative = level - tail_level_;
                if (relative < 3u) out.mip_x = 16u >> relative;
                else out.mip_y = 16u >> (relative - 2u);
            }
        }
        return out;
    }

    void Extents(std::vector<AdrenoA2xxByteRange>& out) const;

private:
    std::array<uint32_t, 6> state_;
    uint32_t                bytes_;
    uint32_t                pitch_;
    uint32_t                width_;
    uint32_t                height_;
    uint32_t                last_level_;
    uint32_t                tail_level_;
    bool                    cube_;
};

}
