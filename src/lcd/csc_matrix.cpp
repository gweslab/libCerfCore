#include "csc_matrix.h"

#include <algorithm>

namespace cerfcore {

std::array<uint32_t, 3> CscConvert(const CscMatrix& csc, const std::array<int32_t, 3>& in) {
    std::array<int32_t, 3> biased{};
    for (uint32_t i = 0; i < 3u; ++i) {
        biased[i] = std::clamp(in[i], csc.pre_clamp[2u * i], csc.pre_clamp[2u * i + 1u]) + csc.pre_bias[i];
    }
    std::array<uint32_t, 3> out{};
    for (uint32_t r = 0; r < 3u; ++r) {
        const int32_t sum = csc.matrix[3u * r] * biased[0] + csc.matrix[3u * r + 1u] * biased[1] +
                            csc.matrix[3u * r + 2u] * biased[2];
        const int32_t value = ((sum + (1 << (kCscFractionBits - 1u))) >> kCscFractionBits) + csc.post_bias[r];
        out[r] = static_cast<uint32_t>(std::clamp(value, csc.post_clamp[2u * r], csc.post_clamp[2u * r + 1u]));
    }
    return out;
}

}
