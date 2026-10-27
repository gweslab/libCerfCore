#include "wave_out_backend.h"
#include "wave_core_audio_format.h"
#include "wave_out_queued_backend.h"
#include "wave_pcm_format.h"

#include <cerfcore/core/log.h>

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>

namespace cerfcore {

namespace {

class CoreAudioWaveOutBackend final : public WaveOutQueuedBackend {
public:
    CoreAudioWaveOutBackend() = default;
    CoreAudioWaveOutBackend(const CoreAudioWaveOutBackend&)            = delete;
    CoreAudioWaveOutBackend& operator=(const CoreAudioWaveOutBackend&) = delete;

    void Stop() override;
    void Reset() override;

private:
    struct Stream {
        CoreAudioWaveOutBackend* owner;
        uint8_t                  silence;
    };

    static OSStatus Render(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* stamp,
                           UInt32 bus, UInt32 frames, AudioBufferList* io);
    static OSStatus CreateUnit(const WaveOutFormat& format, Stream* stream, AudioUnit& unit,
                               const char*& step);
    void FillLocked(uint8_t* out, uint32_t bytes);
    void DropQueuedLocked();

    bool DeviceOpenLocked() const override { return unit_ != nullptr; }
    void CloseDeviceLocked(std::unique_lock<std::mutex>& lk) override;
    bool OpenDeviceLocked(std::unique_lock<std::mutex>& lk) override;
    bool DevicePositionLocked(uint32_t& samples) const override;

    uint32_t  head_offset_   = 0;
    AudioUnit unit_          = nullptr;
    Stream*   stream_        = nullptr;
    uint32_t  frame_bytes_   = 0;
    uint64_t  frames_played_ = 0;
    OSStatus  last_fail_     = noErr;
};

void CoreAudioWaveOutBackend::Stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        shutdown_ = true;
    }
    callbacks_.Stop();
    std::unique_lock<std::mutex> lk(mtx_);
    if (unit_ != nullptr) CloseDeviceLocked(lk);
}

OSStatus CoreAudioWaveOutBackend::Render(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                                         UInt32, UInt32, AudioBufferList* io) {
    Stream* const                  stream = static_cast<Stream*>(ref);
    CoreAudioWaveOutBackend* const self   = stream->owner;
    std::lock_guard<std::mutex>    lk(self->mtx_);
    for (UInt32 i = 0; i < io->mNumberBuffers; ++i) {
        uint8_t* const out   = static_cast<uint8_t*>(io->mBuffers[i].mData);
        const uint32_t bytes = io->mBuffers[i].mDataByteSize;
        if (self->stream_ == stream) {
            self->FillLocked(out, bytes);
        } else {
            std::memset(out, stream->silence, bytes);
        }
    }
    return noErr;
}

void CoreAudioWaveOutBackend::FillLocked(uint8_t* out, uint32_t bytes) {
    uint32_t filled = 0;
    while (filled < bytes && !writes_.empty()) {
        const Block    block = writes_.front();
        const uint32_t n     = std::min(bytes - filled, block.length - head_offset_);
        std::memcpy(out + filled, block.bytes + head_offset_, n);
        filled += n;
        head_offset_ += n;
        if (head_offset_ == block.length) {
            writes_.pop_front();
            head_offset_ = 0;
            callbacks_.PostDone(block.slot);
        }
    }
    frames_played_ += filled / frame_bytes_;
    if (filled < bytes) std::memset(out + filled, stream_->silence, bytes - filled);
}

OSStatus CoreAudioWaveOutBackend::CreateUnit(const WaveOutFormat& format, Stream* stream,
                                             AudioUnit& unit, const char*& step) {
    AudioComponentDescription desc{};
    desc.componentType         = kAudioUnitType_Output;
    desc.componentSubType      = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    const AudioComponent component = ::AudioComponentFindNext(nullptr, &desc);
    if (component == nullptr) {
        step = "AudioComponentFindNext";
        return kAudio_ParamError;
    }
    const AudioStreamBasicDescription fmt = LinearPcmDescription(format.rate_hz, format.channels, format.bits);
    AURenderCallbackStruct callback{&Render, stream};
    OSStatus               err = noErr;
    if ((err = ::AudioComponentInstanceNew(component, &unit)) != noErr) {
        step = "AudioComponentInstanceNew";
    } else if ((err = ::AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
                                             kAudioUnitScope_Input, 0, &fmt, sizeof(fmt))) != noErr) {
        step = "StreamFormat";
    } else if ((err = ::AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
                                             kAudioUnitScope_Input, 0, &callback,
                                             sizeof(callback))) != noErr) {
        step = "SetRenderCallback";
    } else if ((err = ::AudioUnitInitialize(unit)) != noErr) {
        step = "AudioUnitInitialize";
    }
    return err;
}

bool CoreAudioWaveOutBackend::OpenDeviceLocked(std::unique_lock<std::mutex>& lk) {
    policy_.NoteAttempt();
    const WaveOutFormat format = policy_.Requested();
    auto*       stream = new Stream{this, static_cast<uint8_t>(format.bits == 8u ? 0x80u : 0u)};
    AudioUnit   unit   = nullptr;
    OSStatus    err    = kAudio_ParamError;
    const char* step   = "format";
    lk.unlock();
    if (PcmFormatSupported(format.rate_hz, format.channels, format.bits)) {
        step = nullptr;
        err  = CreateUnit(format, stream, unit, step);
    }
    lk.lock();
    if (step == nullptr && !shutdown_) {
        unit_          = unit;
        stream_        = stream;
        frame_bytes_   = PcmFrameBytes(format.channels, format.bits);
        frames_played_ = 0;
        head_offset_   = 0;
        last_fail_     = noErr;
        policy_.NoteOpened();
        ++generation_;
        lk.unlock();
        err = ::AudioOutputUnitStart(unit);
        lk.lock();
        if (err == noErr) {
            LOG(Periph, "[%s] CoreAudio playback opened %u Hz x %u ch x %u bit\n", log_tag_,
                format.rate_hz, format.channels, format.bits);
            return true;
        }
        step    = "AudioOutputUnitStart";
        unit_   = nullptr;
        stream_ = nullptr;
        DropQueuedLocked();
    }
    lk.unlock();
    if (unit != nullptr) {
        ::AudioUnitUninitialize(unit);
        ::AudioComponentInstanceDispose(unit);
    }
    delete stream;
    lk.lock();
    if (step != nullptr && err != last_fail_) {
        LOG(Caution, "%s: CoreAudio playback open (%u Hz x %u ch x %u bit) failed at %s: OSStatus %d "
                     "- silent-mode (blocks paced from their own duration)\n",
            log_tag_, format.rate_hz, format.channels, format.bits, step, static_cast<int>(err));
    }
    if (step != nullptr) last_fail_ = err;
    return false;
}

void CoreAudioWaveOutBackend::DropQueuedLocked() {
    CompleteQueuedLocked();
    head_offset_ = 0;
}

void CoreAudioWaveOutBackend::CloseDeviceLocked(std::unique_lock<std::mutex>& lk) {
    AudioUnit const unit   = unit_;
    Stream* const   stream = stream_;
    unit_                  = nullptr;
    stream_                = nullptr;
    DropQueuedLocked();
    lk.unlock();
    ::AudioOutputUnitStop(unit);
    ::AudioUnitUninitialize(unit);
    ::AudioComponentInstanceDispose(unit);
    delete stream;
    lk.lock();
}

bool CoreAudioWaveOutBackend::DevicePositionLocked(uint32_t& samples) const {
    samples = static_cast<uint32_t>(frames_played_);
    return true;
}

void CoreAudioWaveOutBackend::Reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (unit_ != nullptr) DropQueuedLocked();
    callbacks_.CompleteSilent();
}

}

std::unique_ptr<WaveOutBackend> WaveOutBackend::Create() { return std::make_unique<CoreAudioWaveOutBackend>(); }

}
