#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include "wave_out_backend.h"

#include "host_message_thread.h"
#include "host_wait_timer.h"
#include "silent_block_pacer.h"
#include "wave_winmm_format.h"

#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>

namespace cerfcore {

namespace {

class WinmmWaveOutBackend final : public WaveOutBackend {
public:
    using Clock = std::chrono::steady_clock;

    WinmmWaveOutBackend() = default;
    ~WinmmWaveOutBackend() override;
    WinmmWaveOutBackend(const WinmmWaveOutBackend&)            = delete;
    WinmmWaveOutBackend& operator=(const WinmmWaveOutBackend&) = delete;

    void Stop() override { thread_.Stop(); }
    void PostCommand(uint32_t command) override { thread_.Post(kMsgCommand, command); }
    void Play(uint32_t slot, const uint8_t* bytes, uint32_t length) override;
    void Reset() override;

private:
    struct OpenRequest {
        WAVEFORMATEX fmt;
        DWORD        flags;
    };

    static constexpr uint32_t kSlots      = WaveOutSink::kSlots;
    static constexpr uint32_t kNoSlot     = WaveOutSink::kNoSlot;
    static constexpr WPARAM   kSilentDone = 1;
    static constexpr UINT     kMsgCommand = WM_USER + 41;
    static constexpr UINT     kMsgRetryOpen = WM_USER + 907;
    static_assert(kSlots <= SilentBlockPacer::kCapacity,
                  "every busy slot can hold one silent-mode completion");

    void Begin(StartHandler on_start, CommandHandler on_command, DoneHandler on_done) override;
    HANDLE CreateSilentTimer();
    void OnThreadMessage(const MSG& msg, const CommandHandler& on_command, const DoneHandler& on_done);
    void OnDone(WAVEHDR* hdr, const DoneHandler& on_done);
    void PlayHeader(WAVEHDR* hdr);
    void Unprepare(WAVEHDR* hdr);
    void ArmSilentTimer(Clock::duration delay);
    OpenRequest BeginOpenLocked();
    MMRESULT OpenDevice(const OpenRequest& req, HWAVEOUT& device) const;
    bool FinishOpenLocked(const OpenRequest& req, MMRESULT r, HWAVEOUT device);
    void CloseDeviceLocked();
    void LoseDeviceLocked(const char* call, MMRESULT result);
    void RetryOpen();
    void QueueSilentLocked(WAVEHDR* hdr);
    void DeliverDueSilent(const DoneHandler& on_done);
    void CancelSilentLocked();

    bool DeviceOpenLocked() const override { return out_device_ != nullptr; }
    void CloseDeviceLocked(std::unique_lock<std::mutex>&) override { CloseDeviceLocked(); }
    bool OpenDeviceLocked(std::unique_lock<std::mutex>& lk) override;
    bool DevicePositionLocked(uint32_t& samples) const override;

    WAVEHDR             headers_[kSlots] = {};
    SilentBlockPacer    silent_;
    host_platform::HostWaitTimer silent_timer_;
    MMRESULT            last_fail_  = MMSYSERR_NOERROR;

    host_platform::HostMessageThread thread_;

    HWAVEOUT out_device_ = nullptr;
};

WinmmWaveOutBackend::~WinmmWaveOutBackend() {
    std::lock_guard<std::mutex> lk(mtx_);
    CloseDeviceLocked();
}

void WinmmWaveOutBackend::Begin(StartHandler on_start, CommandHandler on_command, DoneHandler on_done) {
    thread_.Start(*threads_, [this] { return CreateSilentTimer(); }, on_start,
                  [this, on_done] { DeliverDueSilent(on_done); },
                  [this, on_command, on_done](const MSG& msg) { OnThreadMessage(msg, on_command, on_done); });
}

HANDLE WinmmWaveOutBackend::CreateSilentTimer() {
    if (!silent_timer_.Create()) {
        fatal_->Die("%s: CreateWaitableTimer failed; block pacing has no clock", log_tag_);
    }
    return static_cast<HANDLE>(silent_timer_.NativeHandle());
}

void WinmmWaveOutBackend::OnThreadMessage(const MSG& msg, const CommandHandler& on_command,
                                          const DoneHandler& on_done) {
    if (msg.message == kMsgRetryOpen) {
        RetryOpen();
    } else if (msg.message == kMsgCommand) {
        if (on_command) on_command(static_cast<uint32_t>(msg.wParam));
    } else if (msg.message == MM_WOM_DONE) {
        OnDone(reinterpret_cast<WAVEHDR*>(msg.lParam), on_done);
    }
}

void WinmmWaveOutBackend::OnDone(WAVEHDR* hdr, const DoneHandler& on_done) {
    Unprepare(hdr);
    uint32_t slot = kNoSlot;
    for (uint32_t i = 0; i < kSlots; ++i) {
        if (hdr == &headers_[i]) {
            slot = i;
            break;
        }
    }
    if (on_done) on_done(slot);
}

void WinmmWaveOutBackend::ArmSilentTimer(Clock::duration delay) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count();
    if (!silent_timer_.Arm(ns)) {
        fatal_->Die("%s: SetWaitableTimer failed; a block would never be reported", log_tag_);
    }
}

void WinmmWaveOutBackend::QueueSilentLocked(WAVEHDR* hdr) {
    if (!hdr) {
        fatal_->Die("%s: silent completion for a null header", log_tag_);
    }
    const WaveOutFormat& format = policy_.Requested();
    if (silent_.Queue(*fatal_, reinterpret_cast<uintptr_t>(hdr), hdr->dwBufferLength, format.rate_hz,
                      format.channels, format.bits, log_tag_)) {
        ArmSilentTimer(silent_.NextDue() - Clock::now());
    }
}

void WinmmWaveOutBackend::DeliverDueSilent(const DoneHandler& on_done) {
    uintptr_t done[SilentBlockPacer::kCapacity] = {};
    uint32_t  n = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto now = Clock::now();
        n = silent_.TakeDue(now, done);
        if (!silent_.Empty()) ArmSilentTimer(silent_.NextDue() - now);
    }
    for (uint32_t i = 0; i < n; ++i) OnDone(reinterpret_cast<WAVEHDR*>(done[i]), on_done);
}

void WinmmWaveOutBackend::CancelSilentLocked() {
    silent_timer_.Cancel();
    uintptr_t      done[SilentBlockPacer::kCapacity] = {};
    const uint32_t n = silent_.TakeAll(done);
    for (uint32_t i = 0; i < n; ++i) thread_.Post(MM_WOM_DONE, kSilentDone, static_cast<LPARAM>(done[i]));
}

void WinmmWaveOutBackend::CloseDeviceLocked() {
    if (out_device_) {
        waveOutReset(out_device_);
        const MMRESULT r = waveOutClose(out_device_);
        if (r != MMSYSERR_NOERROR) {
            LOG(Caution, "%s: waveOutClose failed mmresult=%u - device handle leaked\n",
                log_tag_, r);
        }
        out_device_ = nullptr;
    }
}

void WinmmWaveOutBackend::LoseDeviceLocked(const char* call, MMRESULT result) {
    const MMRESULT reset = waveOutReset(out_device_);
    const MMRESULT close = waveOutClose(out_device_);
    LOG(Caution, "%s: host audio device lost (%s mmresult=%u, reset %u, close %u) "
            "- silent-mode (blocks paced from their own duration)\n",
            log_tag_, call, result, reset, close);
    out_device_ = nullptr;
    last_fail_  = result;
    policy_.NoteAttempt();
}

WinmmWaveOutBackend::OpenRequest WinmmWaveOutBackend::BeginOpenLocked() {
    policy_.NoteAttempt();
    const WaveOutFormat& format = policy_.Requested();
    OpenRequest req{};
    req.fmt   = PcmWaveFormat(format.rate_hz, format.channels, format.bits);
    req.flags = CALLBACK_THREAD;
    return req;
}

MMRESULT WinmmWaveOutBackend::OpenDevice(const OpenRequest& req, HWAVEOUT& device) const {
    device = nullptr;
    return waveOutOpen(&device, WAVE_MAPPER, &req.fmt, thread_.ThreadId(), 0, req.flags);
}

bool WinmmWaveOutBackend::FinishOpenLocked(const OpenRequest& req, MMRESULT r, HWAVEOUT device) {
    const uint32_t rate     = req.fmt.nSamplesPerSec;
    const uint16_t channels = req.fmt.nChannels;
    const uint16_t bits     = req.fmt.wBitsPerSample;
    if (r != MMSYSERR_NOERROR) {
        if (r != last_fail_) {
            LOG(Caution, "%s: waveOutOpen(%u Hz x %u ch x %u bit) failed mmresult=%u "
                    "- silent-mode (blocks paced from their own duration)\n",
                    log_tag_, rate, channels, bits, r);
        }
        last_fail_ = r;
        return false;
    }
    out_device_ = device;
    last_fail_  = MMSYSERR_NOERROR;
    policy_.NoteOpened();
    ++generation_;
    LOG(Periph, "[%s] waveOut opened %u Hz x %u ch x %u bit\n", log_tag_, rate, channels, bits);
    return true;
}

bool WinmmWaveOutBackend::OpenDeviceLocked(std::unique_lock<std::mutex>&) {
    const OpenRequest req = BeginOpenLocked();
    HWAVEOUT device = nullptr;
    const MMRESULT r = OpenDevice(req, device);
    return FinishOpenLocked(req, r, device);
}

void WinmmWaveOutBackend::RetryOpen() {
    OpenRequest req{};
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!policy_.BeginRetry(out_device_ != nullptr)) return;
        req = BeginOpenLocked();
    }
    HWAVEOUT device = nullptr;
    const MMRESULT r = OpenDevice(req, device);
    std::lock_guard<std::mutex> lk(mtx_);
    FinishOpenLocked(req, r, device);
}

bool WinmmWaveOutBackend::DevicePositionLocked(uint32_t& samples) const {
    MMTIME mmt{};
    mmt.wType = TIME_SAMPLES;
    if (waveOutGetPosition(out_device_, &mmt, sizeof(mmt)) != MMSYSERR_NOERROR ||
        mmt.wType != TIME_SAMPLES)
        return false;
    samples = mmt.u.sample;
    return true;
}

void WinmmWaveOutBackend::Play(uint32_t slot, const uint8_t* bytes, uint32_t length) {
    WAVEHDR* hdr = &headers_[slot];
    std::memset(hdr, 0, sizeof(*hdr));
    hdr->lpData         = reinterpret_cast<LPSTR>(const_cast<uint8_t*>(bytes));
    hdr->dwBufferLength = length;
    PlayHeader(hdr);
}

void WinmmWaveOutBackend::PlayHeader(WAVEHDR* hdr) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (out_device_) {
        const char* call = "waveOutPrepareHeader";
        MMRESULT r = waveOutPrepareHeader(out_device_, hdr, sizeof(WAVEHDR));
        if (r == MMSYSERR_NOERROR) {
            call = "waveOutWrite";
            r = waveOutWrite(out_device_, hdr, sizeof(WAVEHDR));
            if (r == MMSYSERR_NOERROR) return;
            waveOutUnprepareHeader(out_device_, hdr, sizeof(WAVEHDR));
        }
        if (r != MMSYSERR_NODRIVER) {
            fatal_->Die("%s: %s failed mmresult=%u", log_tag_, call, r);
        }
        LoseDeviceLocked(call, r);
    }
    QueueSilentLocked(hdr);
    if (policy_.ShouldPostRetry()) thread_.Post(kMsgRetryOpen);
}

void WinmmWaveOutBackend::Unprepare(WAVEHDR* hdr) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (out_device_ && hdr) waveOutUnprepareHeader(out_device_, hdr, sizeof(WAVEHDR));
}

void WinmmWaveOutBackend::Reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (out_device_) waveOutReset(out_device_);
    CancelSilentLocked();
}

}

std::unique_ptr<WaveOutBackend> WaveOutBackend::Create() { return std::make_unique<WinmmWaveOutBackend>(); }

}
