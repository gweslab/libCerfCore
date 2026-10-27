#pragma once

#include <windows.h>
#include <mmsystem.h>

#include <cstdint>

namespace cerfcore {

WAVEFORMATEX PcmWaveFormat(uint32_t rate_hz, uint16_t channels, uint16_t bits);

}
