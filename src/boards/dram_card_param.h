#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

namespace cerfcore {

namespace BoardConfigParamId {

inline constexpr std::string_view DramCard = "dram_card";

}

inline constexpr std::pair<std::string_view, uint32_t> kDramCardBytes[] = {
    {"none", 0u},
    {"1mb", 1u << 20},
    {"2mb", 2u << 20},
    {"4mb", 4u << 20},
    {"8mb", 8u << 20},
    {"16mb", 16u << 20},
    {"32mb", 32u << 20},
};

}
