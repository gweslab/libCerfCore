#pragma once

#include <array>
#include <cstdint>

namespace cerfcore {

struct CscMatrix {
    std::array<int32_t, 9> matrix{};
    std::array<int32_t, 3> pre_bias{};
    std::array<int32_t, 3> post_bias{};
    std::array<int32_t, 6> pre_clamp{};
    std::array<int32_t, 6> post_clamp{};
};

inline constexpr uint32_t kCscFractionBits = 9u;

std::array<uint32_t, 3> CscConvert(const CscMatrix& csc, const std::array<int32_t, 3>& in);

}
