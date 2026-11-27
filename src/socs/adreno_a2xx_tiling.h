#pragma once
#include <cstdint>

namespace cerfcore {

uint64_t AdrenoA2xxTiledAddress(uint32_t base, uint32_t pitch, uint32_t bytes,
    uint32_t x, uint32_t y);

struct AdrenoA2xxByteRange {
    uint64_t begin = 0;
    uint64_t end   = 0;
};

AdrenoA2xxByteRange AdrenoA2xxTiledRows(uint64_t base, uint32_t pitch, uint32_t bytes,
    uint32_t first_row, uint32_t end_row);

}
