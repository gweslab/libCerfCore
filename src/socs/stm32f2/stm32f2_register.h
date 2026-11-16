#pragma once

#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

inline uint32_t MergeRegisterWrite(uint32_t old, uint32_t offset, uint32_t size, uint32_t value) {
    const uint32_t shift = (offset & 3u) * 8u;
    const uint32_t mask = (size == 4u ? 0xFFFFFFFFu : ((1u << (size * 8u)) - 1u)) << shift;
    return (old & ~mask) | ((value << shift) & mask);
}

}

}
