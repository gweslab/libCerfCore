#pragma once

#include <cstdint>

namespace cerfcore {

inline uint16_t ArmMmuDisabledDataParAttributes() {
    return static_cast<uint16_t>((1u << 4) | (1u << 7));
}

}
