#include "wave_out_queued_backend.h"

#include <utility>

namespace cerfcore {

void WaveOutQueuedBackend::Begin(StartHandler on_start, CommandHandler on_command, DoneHandler on_done) {
    StartWorker();
    callbacks_.Start(*fatal_, *threads_, std::move(on_start), std::move(on_command), std::move(on_done),
                     [this] { RetryOpen(); });
}

void WaveOutQueuedBackend::PostCommand(uint32_t command) { callbacks_.PostCommand(command); }

void WaveOutQueuedBackend::Play(uint32_t slot, const uint8_t* bytes, uint32_t length) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (DeviceOpenLocked()) {
        writes_.push_back(Block{slot, bytes, length});
        work_cv_.notify_all();
        return;
    }
    const WaveOutFormat& req = policy_.Requested();
    callbacks_.QueueSilent(slot, length, req.rate_hz, req.channels, req.bits, log_tag_);
    if (policy_.ShouldPostRetry()) callbacks_.PostRetryOpen();
}

void WaveOutQueuedBackend::CompleteQueuedLocked() {
    for (const Block& b : writes_) callbacks_.PostDone(b.slot);
    writes_.clear();
}

void WaveOutQueuedBackend::RetryOpen() {
    std::unique_lock<std::mutex> lk(mtx_);
    if (policy_.BeginRetry(DeviceOpenLocked()) && !shutdown_) OpenDeviceLocked(lk);
}

}
