#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace cerfcore {
namespace qcom_tile {

const uint32_t kWidth       = 64u;
const uint32_t kHeight      = 32u;
const uint32_t kBytes       = kWidth * kHeight;
const uint32_t kStrideAlign = 128u;

inline uint32_t Columns(uint32_t frame_width) {
    return (frame_width + kStrideAlign - 1u) / kStrideAlign * (kStrideAlign / kWidth);
}

inline uint32_t Rows(uint32_t plane_height) { return (plane_height + kHeight - 1u) / kHeight; }

inline uint32_t Index(uint32_t tx, uint32_t ty, uint32_t cols, uint32_t rows) {
    const uint32_t pair_base = (ty / 2u) * 2u * cols;
    if ((rows & 1u) != 0u && ty == rows - 1u) return pair_base + tx;
    const uint32_t group = tx / 2u, upper = ty & 1u;
    const uint32_t row_in_group = (group & 1u) != 0u ? 1u - upper : upper;
    return pair_base + group * 4u + row_in_group * 2u + (tx & 1u);
}

inline uint32_t Offset(uint32_t x, uint32_t y, uint32_t cols, uint32_t rows) {
    return Index(x / kWidth, y / kHeight, cols, rows) * kBytes + (y % kHeight) * kWidth + (x % kWidth);
}

}
}
