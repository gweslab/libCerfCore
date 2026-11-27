#pragma once
#include "adreno_a2xx_shader.h"
#include <array>
#include <cstdint>

namespace cerfcore {

class AdrenoA2xxColorLayout {
public:
    AdrenoA2xxColorLayout(uint32_t format, uint32_t swap);

    uint32_t       Bytes() const;
    uint32_t       PackedChannels() const;
    uint32_t       Shift(unsigned channel) const;
    uint32_t       Maximum(unsigned channel) const;
    AdrenoA2xxVec4 Unpack(const uint8_t* pixel) const;

private:
    uint32_t                bytes_;
    uint32_t                packed_channels_;
    std::array<uint32_t, 4> shifts_;
    std::array<uint32_t, 4> maxima_;
};

}
