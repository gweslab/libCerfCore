#include "wave_winmm_format.h"

#include "wave_pcm_format.h"

namespace cerfcore {

WAVEFORMATEX PcmWaveFormat(uint32_t rate_hz, uint16_t channels, uint16_t bits) {
    WAVEFORMATEX fmt{};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = channels;
    fmt.nSamplesPerSec  = rate_hz;
    fmt.wBitsPerSample  = bits;
    fmt.nBlockAlign     = static_cast<uint16_t>(PcmFrameBytes(channels, bits));
    fmt.nAvgBytesPerSec = rate_hz * fmt.nBlockAlign;
    fmt.cbSize          = 0;
    return fmt;
}

}
