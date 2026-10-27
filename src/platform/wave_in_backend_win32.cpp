#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include "wave_in_backend.h"

#include "host_message_thread.h"
#include "wave_winmm_format.h"

#include <cerfcore/core/log.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace cerfcore {

namespace {

constexpr UINT kMsgOpen = WM_USER + 0x10u;

class WaveInSink {
public:
    using MessageHandler = std::function<void(const MSG&)>;

    WaveInSink() = default;
    ~WaveInSink();
    WaveInSink(const WaveInSink&)            = delete;
    WaveInSink& operator=(const WaveInSink&) = delete;

    void Start(MessageHandler on_message, const char* log_tag);
    void Stop() { thread_.Stop(); }

    void Post(UINT message) const { thread_.Post(message); }

    bool EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);

    void Requeue(WAVEHDR* hdr);

private:
    static constexpr uint32_t kRecordBuffers  = 8u;
    static constexpr uint32_t kRecordBufBytes = 4096u;

    void CloseDevice();

    host_platform::HostMessageThread thread_;

    HWAVEIN  in_device_     = nullptr;
    uint32_t open_rate_     = 0;
    uint16_t open_channels_ = 0;
    uint16_t open_bits_     = 0;

    struct RecBuf {
        WAVEHDR              hdr{};
        std::vector<uint8_t> bytes;
    };
    RecBuf      bufs_[kRecordBuffers];
    const char* log_tag_ = "WaveInSink";
};

WaveInSink::~WaveInSink() {
    Stop();
    CloseDevice();
}

void WaveInSink::Start(MessageHandler on_message, const char* log_tag) {
    log_tag_ = log_tag;
    thread_.Start(nullptr, nullptr, nullptr, on_message);
}

void WaveInSink::CloseDevice() {
    if (!in_device_) return;
    waveInReset(in_device_);
    for (auto& b : bufs_) {
        if (b.hdr.dwFlags & WHDR_PREPARED)
            waveInUnprepareHeader(in_device_, &b.hdr, sizeof(WAVEHDR));
    }
    waveInClose(in_device_);
    in_device_ = nullptr;
}

bool WaveInSink::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels,
                              uint16_t bits) {
    if (in_device_ && open_rate_ == sample_rate_hz &&
        open_channels_ == channels && open_bits_ == bits) {
        return true;
    }
    if (in_device_) CloseDevice();

    WAVEFORMATEX fmt = PcmWaveFormat(sample_rate_hz, channels, bits);

    MMRESULT r = waveInOpen(&in_device_, WAVE_MAPPER, &fmt,
                            thread_.ThreadId(), 0, CALLBACK_THREAD);
    if (r != MMSYSERR_NOERROR) {
        LOG(Caution, "%s: waveInOpen(%u Hz x %u ch x %u bit) failed mmresult=%u "
                "- microphone capture unavailable\n",
                log_tag_, sample_rate_hz, channels, bits, r);
        in_device_ = nullptr;
        return false;
    }

    for (auto& b : bufs_) {
        b.bytes.assign(kRecordBufBytes, 0);
        b.hdr = WAVEHDR{};
        b.hdr.lpData         = reinterpret_cast<LPSTR>(b.bytes.data());
        b.hdr.dwBufferLength = kRecordBufBytes;
        if (waveInPrepareHeader(in_device_, &b.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
            waveInAddBuffer(in_device_, &b.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            LOG(Caution, "%s: record buffer prime failed\n", log_tag_);
            CloseDevice();
            return false;
        }
    }

    if (waveInStart(in_device_) != MMSYSERR_NOERROR) {
        LOG(Caution, "%s: waveInStart failed\n", log_tag_);
        CloseDevice();
        return false;
    }

    open_rate_     = sample_rate_hz;
    open_channels_ = channels;
    open_bits_     = bits;
    LOG(Periph, "[%s] waveIn opened %u Hz x %u ch x %u bit\n",
        log_tag_, sample_rate_hz, channels, bits);
    return true;
}

void WaveInSink::Requeue(WAVEHDR* hdr) {
    if (!in_device_ || !hdr) return;
    hdr->dwBytesRecorded = 0;
    hdr->dwFlags &= ~WHDR_DONE;
    waveInAddBuffer(in_device_, hdr, sizeof(WAVEHDR));
}

class WinmmWaveInBackend final : public WaveInBackend {
public:
    void Stop() override { sink_.Stop(); }

private:
    void Begin() override;
    void OnRequest() override { sink_.Post(kMsgOpen); }
    void OnThreadMessage(const MSG& msg);
    void OpenOnThread();

    WaveInSink sink_;
};

void WinmmWaveInBackend::Begin() {
    sink_.Start([this](const MSG& msg) { OnThreadMessage(msg); }, log_tag_);
}

void WinmmWaveInBackend::OnThreadMessage(const MSG& msg) {
    if (msg.message == kMsgOpen) {
        OpenOnThread();
    } else if (msg.message == MM_WIM_DATA) {
        auto* hdr = reinterpret_cast<LPWAVEHDR>(msg.lParam);
        if (!hdr) return;
        fifo_.Push(reinterpret_cast<const uint8_t*>(hdr->lpData), hdr->dwBytesRecorded);
        sink_.Requeue(hdr);
    }
}

void WinmmWaveInBackend::OpenOnThread() {
    WaveInFormat format;
    if (!request_.TakeChange(format)) return;
    Clear();
    if (!sink_.EnsureFormat(format.rate_hz, format.channels, format.bits)) {
        LOG(Caution, "[%s] microphone capture unavailable at %u Hz (host waveIn open failed); "
                     "the guest records silence\n", log_tag_, format.rate_hz);
    }
}

}

std::unique_ptr<WaveInBackend> WaveInBackend::Create() { return std::make_unique<WinmmWaveInBackend>(); }

}
