#pragma once

#include <cstdint>

namespace cerfcore {

enum class PinDrive : uint8_t {
    kLow,
    kHigh,
    kPulledUp,
    kUndriven,
    kFunction,
};

}
