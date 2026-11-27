#pragma once

#include <cstdint>

namespace cerfcore {

struct AdrenoA2xxMmu {
    uint32_t config   = 0;
    uint32_t va_range = 0;
    uint32_t pt_base  = 0;
    uint32_t mpu_base = 0;
    uint32_t mpu_end  = 0;
    bool     pixel_stage = false;
};

}
