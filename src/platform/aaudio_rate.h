#pragma once

#include <cstdint>

namespace cerfcore {

/* Android system/audio.h SAMPLE_RATE_HZ_MIN_AAUDIO; AAudioStreamParameters::validate. */
inline constexpr uint32_t kAaudioMinRateHz = 8000u;

uint32_t AaudioRateMultiple(uint32_t rate_hz);

}
