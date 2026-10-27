#include "wave_core_audio_format.h"

#include "wave_pcm_format.h"

namespace cerfcore {

AudioStreamBasicDescription LinearPcmDescription(uint32_t rate_hz, uint16_t channels, uint16_t bits) {
    const uint32_t              frame = PcmFrameBytes(channels, bits);
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate       = rate_hz;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = (bits == 16u ? kAudioFormatFlagIsSignedInteger : 0u) | kAudioFormatFlagIsPacked;
    fmt.mBytesPerPacket   = frame;
    fmt.mFramesPerPacket  = 1u;
    fmt.mBytesPerFrame    = frame;
    fmt.mChannelsPerFrame = channels;
    fmt.mBitsPerChannel   = bits;
    return fmt;
}

}
