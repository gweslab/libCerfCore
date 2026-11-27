#include "adreno_a2xx_color_layout.h"
#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace {

constexpr uint32_t kFormat4444      = 0u;
constexpr uint32_t kFormat8888      = 5u;
constexpr uint32_t kSwapReversed    = 3u;
constexpr float    kByteMaximum     = 255.0f;

}

AdrenoA2xxColorLayout::AdrenoA2xxColorLayout(uint32_t format, uint32_t swap)
    : bytes_(format == kFormat8888 ? 4u : 2u),
      packed_channels_(format == kFormat4444 ? 4u : 3u),
      shifts_(format == kFormat4444 ? (swap == kSwapReversed ? std::array<uint32_t, 4>{12, 8, 4, 0}
                                                              : std::array<uint32_t, 4>{0, 4, 8, 12})
                                    : std::array<uint32_t, 4>{0, 5, 11, 0}),
      maxima_(format == kFormat4444 ? std::array<uint32_t, 4>{15, 15, 15, 15}
                                    : std::array<uint32_t, 4>{31, 63, 31, 0}) {}

uint32_t AdrenoA2xxColorLayout::Bytes() const { return bytes_; }

uint32_t AdrenoA2xxColorLayout::PackedChannels() const { return packed_channels_; }

uint32_t AdrenoA2xxColorLayout::Shift(unsigned channel) const { return shifts_[channel]; }

uint32_t AdrenoA2xxColorLayout::Maximum(unsigned channel) const { return maxima_[channel]; }

AdrenoA2xxVec4 AdrenoA2xxColorLayout::Unpack(const uint8_t* pixel) const {
    AdrenoA2xxVec4 color{0.0f, 0.0f, 0.0f, 1.0f};
    if (bytes_ == 4u) {
        for (unsigned i = 0; i < 4u; ++i) color[i] = static_cast<float>(pixel[i]) / kByteMaximum;
        return color;
    }
    const uint32_t packed = cerf::le::U16(pixel);
    for (unsigned i = 0; i < packed_channels_; ++i)
        color[i] = static_cast<float>((packed >> shifts_[i]) & maxima_[i]) / static_cast<float>(maxima_[i]);
    return color;
}

}
