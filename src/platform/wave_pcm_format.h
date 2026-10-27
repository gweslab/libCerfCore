#pragma once

#include <cstdint>

namespace cerfcore {

bool     PcmFormatSupported(uint32_t rate_hz, uint16_t channels, uint16_t bits);
uint32_t PcmFrameBytes(uint16_t channels, uint16_t bits);

}
