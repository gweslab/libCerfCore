#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cerfcore {

inline constexpr std::size_t kQseedCoefficientWords = 1024u;

extern const std::array<uint32_t, kQseedCoefficientWords> kQseedCoefficients;

}
