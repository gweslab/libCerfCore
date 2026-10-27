#include "wave_in_backend.h"
#include "wave_in_thread_backend.h"
#include "wave_pcm_format.h"

#include <cerfcore/core/log.h>

#include <alsa/asoundlib.h>

#include <memory>
#include <mutex>
#include <vector>

namespace cerfcore {

namespace {

class AlsaWaveInBackend final : public WaveInThreadBackend {
private:
    static constexpr uint32_t kRecordBufBytes = 4096u;
    /* QEMU audio/alsaaudio.c: buffer_length 92880 us, "4096 frames assuming 44100Hz". */
    static constexpr unsigned kBufferUs = 92880;

    void ThreadMain() override;
    void InterruptLocked() override;
    void OpenLocked(const WaveInFormat& format);
    void CloseLocked();

    snd_pcm_t* pcm_         = nullptr;
    uint32_t   frame_bytes_ = 0;
};

void AlsaWaveInBackend::InterruptLocked() {
    if (pcm_ != nullptr) snd_pcm_drop(pcm_);
}

void AlsaWaveInBackend::OpenLocked(const WaveInFormat& requested) {
    const uint32_t   rate     = requested.rate_hz;
    const uint16_t   channels = requested.channels;
    const uint16_t   bits     = requested.bits;
    snd_pcm_format_t format   = SND_PCM_FORMAT_UNKNOWN;
    if (bits == 16u) format = SND_PCM_FORMAT_S16_LE;
    if (bits == 8u) format = SND_PCM_FORMAT_U8;
    snd_pcm_t* pcm = nullptr;
    int        err = -EINVAL;
    if (format != SND_PCM_FORMAT_UNKNOWN) {
        err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
        if (err == 0) {
            err = snd_pcm_set_params(pcm, format, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate,
                                     1, kBufferUs);
        }
        if (err == 0) err = snd_pcm_start(pcm);
    }
    if (err < 0) {
        if (pcm != nullptr) snd_pcm_close(pcm);
        LOG(Caution, "[%s] microphone capture unavailable at %u Hz (ALSA capture open: %s); "
                     "the guest records silence\n",
            log_tag_, rate, snd_strerror(err));
        return;
    }
    pcm_         = pcm;
    frame_bytes_ = PcmFrameBytes(channels, bits);
    LOG(Periph, "[%s] ALSA capture opened %u Hz x %u ch x %u bit\n", log_tag_, rate, channels,
        bits);
}

void AlsaWaveInBackend::CloseLocked() {
    if (pcm_ == nullptr) return;
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
}

/* QEMU audio/alsaaudio.c alsa_read: -EPIPE (overrun) goes to snd_pcm_recover; alsa-lib
   pcm.c snd_pcm_recover leaves the stream prepared, so capture is started again. */
void AlsaWaveInBackend::ThreadMain() {
    std::vector<uint8_t>         buf(kRecordBufBytes);
    std::unique_lock<std::mutex> lk(mtx_);
    while (!stop_) {
        WaveInFormat format;
        if (request_.TakeChange(format)) {
            CloseLocked();
            fifo_.Clear();
            OpenLocked(format);
            continue;
        }
        if (pcm_ == nullptr) {
            cv_.wait(lk, [&] { return stop_ || request_.Pending(); });
            continue;
        }
        snd_pcm_t* const        pcm         = pcm_;
        const uint32_t          frame_bytes = frame_bytes_;
        lk.unlock();
        const snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf.data(), kRecordBufBytes / frame_bytes);
        if (n > 0) fifo_.Push(buf.data(), static_cast<size_t>(n) * frame_bytes);
        lk.lock();
        if (n >= 0 || stop_ || request_.Pending()) continue;
        int err = snd_pcm_recover(pcm, static_cast<int>(n), 1);
        if (err == 0) err = snd_pcm_start(pcm);
        if (err < 0) {
            LOG(Caution, "[%s] host microphone lost (snd_pcm_readi: %s); the guest records "
                         "silence\n",
                log_tag_, snd_strerror(err));
            CloseLocked();
        }
    }
    CloseLocked();
}

}

std::unique_ptr<WaveInBackend> WaveInBackend::Create() { return std::make_unique<AlsaWaveInBackend>(); }

}
