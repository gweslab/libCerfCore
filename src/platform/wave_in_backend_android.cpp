#include "wave_in_backend.h"
#include "wave_in_thread_backend.h"

#include "aaudio_rate.h"
#include "wave_pcm_format.h"

#include <cerfcore/core/log.h>

#include <aaudio/AAudio.h>

#include <memory>
#include <mutex>
#include <vector>

namespace cerfcore {

namespace {

class AaudioWaveInBackend final : public WaveInThreadBackend {
private:
    static constexpr int32_t kReadFrames  = 1024;
    static constexpr int64_t kReadSliceNs = 20'000'000;

    void ThreadMain() override;
    void Open(const WaveInFormat& format);
    void Close();
    bool Stopping();
    void Deliver(const int16_t* samples, size_t frames);

    AAudioStream*        stream_   = nullptr;
    uint16_t             bits_     = 0;
    uint16_t             channels_ = 0;
    uint32_t             group_    = 1;
    uint32_t             grouped_  = 0;
    std::vector<int32_t> sums_;
    std::vector<int16_t> averaged_;
    std::vector<uint8_t> narrowed_;
};

void AaudioWaveInBackend::Open(const WaveInFormat& requested) {
    aaudio_result_t err   = AAUDIO_ERROR_INVALID_FORMAT;
    const uint32_t  group = AaudioRateMultiple(requested.rate_hz);
    if (PcmFormatSupported(requested.rate_hz, requested.channels, requested.bits)) {
        AAudioStreamBuilder* builder = nullptr;
        err = AAudio_createStreamBuilder(&builder);
        if (err == AAUDIO_OK) {
            AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
            AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(requested.rate_hz * group));
            AAudioStreamBuilder_setChannelCount(builder, requested.channels);
            AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
            err = AAudioStreamBuilder_openStream(builder, &stream_);
            AAudioStreamBuilder_delete(builder);
        }
        if (err == AAUDIO_OK) {
            err = AAudioStream_requestStart(stream_);
            if (err != AAUDIO_OK) Close();
        }
    }
    if (err != AAUDIO_OK) {
        stream_ = nullptr;
        LOG(Caution, "[%s] microphone capture unavailable at %u Hz (AAudio capture open: %s); "
                     "the guest records silence\n",
            log_tag_, requested.rate_hz, AAudio_convertResultToText(err));
        return;
    }
    bits_     = requested.bits;
    channels_ = requested.channels;
    group_    = group;
    grouped_  = 0;
    sums_.assign(channels_, 0);
    LOG(Periph, "[%s] AAudio capture opened %u Hz x %u ch x %u bit (device %u Hz)\n", log_tag_,
        requested.rate_hz, requested.channels, requested.bits, requested.rate_hz * group);
}

void AaudioWaveInBackend::Deliver(const int16_t* samples, size_t frames) {
    const int16_t* out    = samples;
    size_t         count  = frames * channels_;
    if (group_ != 1u) {
        averaged_.clear();
        for (size_t f = 0; f < frames; ++f) {
            for (size_t ch = 0; ch < channels_; ++ch) sums_[ch] += samples[f * channels_ + ch];
            if (++grouped_ != group_) continue;
            for (size_t ch = 0; ch < channels_; ++ch) {
                averaged_.push_back(static_cast<int16_t>(sums_[ch] / static_cast<int32_t>(group_)));
                sums_[ch] = 0;
            }
            grouped_ = 0;
        }
        out   = averaged_.data();
        count = averaged_.size();
    }
    if (count == 0u) return;
    if (bits_ == 8u) {
        narrowed_.resize(count);
        for (size_t i = 0; i < count; ++i) narrowed_[i] = static_cast<uint8_t>((out[i] >> 8) + 128);
        fifo_.Push(narrowed_.data(), count);
        return;
    }
    fifo_.Push(reinterpret_cast<const uint8_t*>(out), count * sizeof(int16_t));
}

void AaudioWaveInBackend::Close() {
    if (stream_ == nullptr) return;
    AAudioStream_close(stream_);
    stream_ = nullptr;
}

bool AaudioWaveInBackend::Stopping() {
    std::lock_guard<std::mutex> lk(mtx_);
    return stop_;
}

void AaudioWaveInBackend::ThreadMain() {
    std::vector<int16_t> samples;
    while (!Stopping()) {
        WaveInFormat format;
        if (request_.TakeChange(format)) {
            Close();
            fifo_.Clear();
            Open(format);
            continue;
        }
        if (stream_ == nullptr) {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [&] { return stop_ || request_.Pending(); });
            continue;
        }
        samples.resize(static_cast<size_t>(kReadFrames) * channels_);
        const aaudio_result_t n = AAudioStream_read(stream_, samples.data(), kReadFrames, kReadSliceNs);
        if (n > 0) Deliver(samples.data(), static_cast<size_t>(n));
        if (n >= 0) continue;
        LOG(Caution, "[%s] host microphone lost (AAudioStream_read: %s); the guest records "
                     "silence\n",
            log_tag_, AAudio_convertResultToText(n));
        Close();
    }
    Close();
}

}

std::unique_ptr<WaveInBackend> WaveInBackend::Create() { return std::make_unique<AaudioWaveInBackend>(); }

}
