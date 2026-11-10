#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

namespace cerfcore {

inline constexpr std::pair<std::string_view, uint32_t> kHp600DramBytes[] = {
    {"4mb", 4u << 20},
    {"8mb", 8u << 20},
    {"12mb", 12u << 20},
    {"16mb", 16u << 20},
    {"32mb", 32u << 20},
};

}
