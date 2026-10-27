#include "wave_out_backend.h"
#include "wave_out_queued_backend.h"
#include "wave_pcm_format.h"

#include <cerfcore/core/log.h>

#include <alsa/asoundlib.h>

#include <cerrno>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace cerfcore {

namespace {

class AlsaWaveOutBackend final : public WaveOutQueuedBackend {
public:
    AlsaWaveOutBackend() = default;
    AlsaWaveOutBackend(const AlsaWaveOutBackend&)            = delete;
    AlsaWaveOutBackend& operator=(const AlsaWaveOutBackend&) = delete;

    void Stop() override;
    void Reset() override;

private:
    /* QEMU audio/alsaaudio.c: buffer_length 92880 us, "4096 frames assuming 44100Hz". */
    static constexpr unsigned kBufferUs    = 92880;
    static constexpr int      kWaitSliceMs = 100;

    void StartWorker() override;
    void WriterMain();
    int  WriteBlock(snd_pcm_t* pcm, const Block& block, uint32_t frame_bytes);
    void LoseDeviceLocked(const char* call, int err);

    bool DeviceOpenLocked() const override { return pcm_ != nullptr; }
    void CloseDeviceLocked(std::unique_lock<std::mutex>& lk) override;
    bool OpenDeviceLocked(std::unique_lock<std::mutex>& lk) override;
    bool DevicePositionLocked(uint32_t& samples) const override;

    std::condition_variable idle_cv_;
    snd_pcm_t*              pcm_            = nullptr;
    bool                    writing_        = false;
    bool                    abort_          = false;
    uint32_t                frame_bytes_    = 0;
    uint64_t                frames_written_ = 0;
    int                     last_fail_      = 0;
    std::thread             writer_thread_;
};

void AlsaWaveOutBackend::StartWorker() { writer_thread_ = std::thread([this] { WriterMain(); }); }

void AlsaWaveOutBackend::Stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        shutdown_ = true;
        if (pcm_ != nullptr) snd_pcm_drop(pcm_);
    }
    work_cv_.notify_all();
    callbacks_.Stop();
    if (writer_thread_.joinable()) writer_thread_.join();
    std::lock_guard<std::mutex> lk(mtx_);
    if (pcm_ != nullptr) {
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}

void AlsaWaveOutBackend::WriterMain() {
    std::unique_lock<std::mutex> lk(mtx_);
    while (true) {
        work_cv_.wait(lk, [this] { return shutdown_ || (!writes_.empty() && pcm_ != nullptr); });
        if (shutdown_) return;
        const Block block = writes_.front();
        writes_.pop_front();
        snd_pcm_t* const pcm         = pcm_;
        const uint32_t   frame_bytes = frame_bytes_;
        writing_                     = true;
        abort_                       = false;
        lk.unlock();
        const int err = WriteBlock(pcm, block, frame_bytes);
        lk.lock();
        writing_ = false;
        idle_cv_.notify_all();
        callbacks_.PostDone(block.slot);
        if (err < 0 && !abort_ && pcm_ == pcm) LoseDeviceLocked("snd_pcm_writei", err);
    }
}

/* alsa-lib pcm.c snd_pcm_writei: -EBADFD outside PREPARED/RUNNING, -EPIPE/-ESTRPIPE for
   snd_pcm_recover; QEMU audio/alsaaudio.c alsa_write treats -EAGAIN as a full ring. */
int AlsaWaveOutBackend::WriteBlock(snd_pcm_t* pcm, const Block& block, uint32_t frame_bytes) {
    const uint8_t*    src    = block.bytes;
    snd_pcm_uframes_t frames = block.length / frame_bytes;
    while (frames != 0u) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (abort_ || shutdown_) return 0;
        const snd_pcm_sframes_t n = snd_pcm_writei(pcm, src, frames);
        if (n > 0) {
            frames_written_ += static_cast<uint64_t>(n);
            src += static_cast<size_t>(n) * frame_bytes;
            frames -= static_cast<snd_pcm_uframes_t>(n);
            continue;
        }
        if (n == -EAGAIN || n == 0) {
            lk.unlock();
            const int ready = snd_pcm_wait(pcm, kWaitSliceMs);
            if (ready >= 0) continue;
            lk.lock();
            if (abort_ || shutdown_) return 0;
            const int recovered = snd_pcm_recover(pcm, ready, 1);
            if (recovered < 0) return recovered;
            continue;
        }
        const int err = n == -EBADFD ? snd_pcm_prepare(pcm)
                                     : snd_pcm_recover(pcm, static_cast<int>(n), 1);
        if (err < 0) return err;
    }
    return 0;
}

bool AlsaWaveOutBackend::OpenDeviceLocked(std::unique_lock<std::mutex>&) {
    policy_.NoteAttempt();
    const WaveOutFormat& req = policy_.Requested();
    int                  err    = 0;
    snd_pcm_t*           pcm    = nullptr;
    snd_pcm_format_t     format = SND_PCM_FORMAT_UNKNOWN;
    if (req.bits == 16u) format = SND_PCM_FORMAT_S16_LE;
    if (req.bits == 8u) format = SND_PCM_FORMAT_U8;
    if (format == SND_PCM_FORMAT_UNKNOWN) {
        err = -EINVAL;
    } else {
        err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
        if (err == 0) {
            err = snd_pcm_set_params(pcm, format, SND_PCM_ACCESS_RW_INTERLEAVED, req.channels,
                                     req.rate_hz, 1, kBufferUs);
        }
    }
    if (err < 0) {
        if (pcm != nullptr) snd_pcm_close(pcm);
        if (err != last_fail_) {
            LOG(Caution, "%s: ALSA playback open (%u Hz x %u ch x %u bit) failed: %s "
                         "- silent-mode (blocks paced from their own duration)\n",
                log_tag_, req.rate_hz, req.channels, req.bits, snd_strerror(err));
        }
        last_fail_ = err;
        return false;
    }
    pcm_            = pcm;
    frame_bytes_    = PcmFrameBytes(req.channels, req.bits);
    frames_written_ = 0;
    last_fail_      = 0;
    policy_.NoteOpened();
    ++generation_;
    LOG(Periph, "[%s] ALSA playback opened %u Hz x %u ch x %u bit\n", log_tag_, req.rate_hz,
        req.channels, req.bits);
    work_cv_.notify_all();
    return true;
}

void AlsaWaveOutBackend::CloseDeviceLocked(std::unique_lock<std::mutex>& lk) {
    abort_ = true;
    snd_pcm_drop(pcm_);
    idle_cv_.wait(lk, [this] { return !writing_; });
    CompleteQueuedLocked();
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
}

void AlsaWaveOutBackend::LoseDeviceLocked(const char* call, int err) {
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
    CompleteQueuedLocked();
    LOG(Caution, "%s: host audio device lost (%s: %s) - silent-mode (blocks paced from their "
                 "own duration)\n",
        log_tag_, call, snd_strerror(err));
    last_fail_ = err;
    policy_.NoteAttempt();
}

/* alsa-lib pcm.c snd_pcm_delay: the frames between the write call and the DAC. */
bool AlsaWaveOutBackend::DevicePositionLocked(uint32_t& samples) const {
    snd_pcm_sframes_t delay = 0;
    if (snd_pcm_delay(pcm_, &delay) < 0 || delay < 0 ||
        static_cast<uint64_t>(delay) > frames_written_) {
        return false;
    }
    samples = static_cast<uint32_t>(frames_written_ - static_cast<uint64_t>(delay));
    return true;
}

void AlsaWaveOutBackend::Reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (pcm_ != nullptr) {
        abort_ = true;
        snd_pcm_drop(pcm_);
        CompleteQueuedLocked();
    }
    callbacks_.CompleteSilent();
}

}

std::unique_ptr<WaveOutBackend> WaveOutBackend::Create() { return std::make_unique<AlsaWaveOutBackend>(); }

}
