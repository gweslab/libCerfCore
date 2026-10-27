#include "wave_in_backend.h"
#include "wave_core_audio_format.h"
#include "wave_in_thread_backend.h"
#include "wave_pcm_format.h"

#include <cerfcore/core/log.h>

#include <AudioToolbox/AudioToolbox.h>

#include <memory>
#include <mutex>

namespace cerfcore {

namespace {

class CoreAudioWaveInBackend final : public WaveInThreadBackend {
private:
    static constexpr uint32_t kRecordBufBytes = 4096u;
    static constexpr uint32_t kRecordBuffers  = 3u;

    static void Input(void* user, AudioQueueRef queue, AudioQueueBufferRef buffer,
                      const AudioTimeStamp* start, UInt32 packets,
                      const AudioStreamPacketDescription* descriptions);
    void ThreadMain() override;
    void OpenLocked(const WaveInFormat& format);
    void CloseLocked();

    AudioQueueRef queue_ = nullptr;
};

void CoreAudioWaveInBackend::Input(void* user, AudioQueueRef queue, AudioQueueBufferRef buffer,
                                   const AudioTimeStamp*, UInt32, const AudioStreamPacketDescription*) {
    CoreAudioWaveInBackend* const self = static_cast<CoreAudioWaveInBackend*>(user);
    if (buffer->mAudioDataByteSize != 0u) {
        self->fifo_.Push(static_cast<const uint8_t*>(buffer->mAudioData), buffer->mAudioDataByteSize);
    }
    ::AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

void CoreAudioWaveInBackend::OpenLocked(const WaveInFormat& requested) {
    const uint32_t rate     = requested.rate_hz;
    const uint16_t channels = requested.channels;
    const uint16_t bits     = requested.bits;
    const uint32_t frame    = PcmFrameBytes(channels, bits);
    OSStatus       err      = kAudio_ParamError;
    const char*    step     = "format";
    AudioQueueRef  queue    = nullptr;
    if (PcmFormatSupported(rate, channels, bits)) {
        const AudioStreamBasicDescription fmt = LinearPcmDescription(rate, channels, bits);
        step = "AudioQueueNewInput";
        err  = ::AudioQueueNewInput(&fmt, &Input, this, nullptr, nullptr, 0, &queue);
        for (uint32_t i = 0; err == noErr && i < kRecordBuffers; ++i) {
            AudioQueueBufferRef buffer = nullptr;
            step = "AudioQueueAllocateBuffer";
            err  = ::AudioQueueAllocateBuffer(queue, kRecordBufBytes / frame * frame, &buffer);
            if (err == noErr) {
                step = "AudioQueueEnqueueBuffer";
                err  = ::AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
            }
        }
        if (err == noErr) {
            step = "AudioQueueStart";
            err  = ::AudioQueueStart(queue, nullptr);
        }
    }
    if (err != noErr) {
        if (queue != nullptr) ::AudioQueueDispose(queue, true);
        LOG(Caution, "[%s] microphone capture unavailable at %u Hz (CoreAudio %s: OSStatus %d); "
                     "the guest records silence\n",
            log_tag_, rate, step, static_cast<int>(err));
        return;
    }
    queue_ = queue;
    LOG(Periph, "[%s] CoreAudio capture opened %u Hz x %u ch x %u bit\n", log_tag_, rate, channels,
        bits);
}

void CoreAudioWaveInBackend::CloseLocked() {
    if (queue_ == nullptr) return;
    ::AudioQueueStop(queue_, true);
    ::AudioQueueDispose(queue_, true);
    queue_ = nullptr;
}

void CoreAudioWaveInBackend::ThreadMain() {
    std::unique_lock<std::mutex> lk(mtx_);
    while (!stop_) {
        WaveInFormat format;
        if (request_.TakeChange(format)) {
            CloseLocked();
            fifo_.Clear();
            OpenLocked(format);
            continue;
        }
        cv_.wait(lk, [&] { return stop_ || request_.Pending(); });
    }
    CloseLocked();
}

}

std::unique_ptr<WaveInBackend> WaveInBackend::Create() { return std::make_unique<CoreAudioWaveInBackend>(); }

}
