#pragma once

#include <cstdint>

namespace cerfcore {

struct EmulationMultiplier {
    uint64_t num = 1u;
    uint64_t den = 1u;

    constexpr bool   IsOne() const { return num == den; }
    constexpr double Value() const { return static_cast<double>(num) / static_cast<double>(den); }
};

}
