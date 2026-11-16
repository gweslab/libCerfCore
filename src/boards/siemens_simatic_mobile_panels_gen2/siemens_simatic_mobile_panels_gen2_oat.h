#pragma once

#include <cstdint>

namespace cerfcore {

constexpr uint32_t kDramVa = 0x80300000u;
constexpr uint32_t kDramPa = 0x10300000u;
constexpr uint32_t kDramSize = 381u * 0x100000u;
constexpr uint32_t kDdrPa = 0x10000000u;
constexpr uint32_t kDdrChipSelectSize = 512u * 0x100000u;

constexpr uint32_t kInitStackTopPa = kDramPa + kDramSize;

}
