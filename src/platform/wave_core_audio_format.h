#pragma once

#include <AudioToolbox/AudioToolbox.h>

#include <cstdint>

namespace cerfcore {

AudioStreamBasicDescription LinearPcmDescription(uint32_t rate_hz, uint16_t channels, uint16_t bits);

}
