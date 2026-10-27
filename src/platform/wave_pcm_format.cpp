#include "wave_pcm_format.h"

namespace cerfcore {

bool PcmFormatSupported(uint32_t rate_hz, uint16_t channels, uint16_t bits) {
    return (bits == 8u || bits == 16u) && channels != 0u && rate_hz != 0u;
}

uint32_t PcmFrameBytes(uint16_t channels, uint16_t bits) { return static_cast<uint32_t>(channels) * (bits / 8u); }

}
