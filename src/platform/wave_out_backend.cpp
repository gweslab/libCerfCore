#include "wave_out_backend.h"

#include <utility>

namespace cerfcore {

void WaveOutBackend::Start(StartHandler on_start, CommandHandler on_command, DoneHandler on_done,
                           const char* log_tag) {
    log_tag_ = log_tag;
    Begin(std::move(on_start), std::move(on_command), std::move(on_done));
}

bool WaveOutBackend::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits, bool busy) {
    std::unique_lock<std::mutex> lk(mtx_);
    return policy_.EnsureFormat(WaveOutFormat{sample_rate_hz, channels, bits}, busy, *this, lk);
}

bool WaveOutBackend::IsOpen() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return DeviceOpenLocked();
}

bool WaveOutBackend::SamplePosition(uint32_t& samples, uint32_t& generation) const {
    std::lock_guard<std::mutex> lk(mtx_);
    generation = generation_;
    return DeviceOpenLocked() && DevicePositionLocked(samples);
}

}
