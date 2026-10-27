#include "wave_out_sink.h"

#include "wave_out_backend.h"

#include <utility>

namespace cerfcore {

WaveOutSink::WaveOutSink() : backend_(WaveOutBackend::Create()) {}

WaveOutSink::~WaveOutSink() { backend_->Stop(); }

void WaveOutSink::Start(StartHandler on_start, CommandHandler on_command, DoneHandler on_done,
                        const char* log_tag) {
    backend_->Start(std::move(on_start), std::move(on_command), std::move(on_done), log_tag);
}

void WaveOutSink::Stop() { backend_->Stop(); }

void WaveOutSink::PostCommand(uint32_t command) { backend_->PostCommand(command); }

bool WaveOutSink::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits, bool busy) {
    return backend_->EnsureFormat(sample_rate_hz, channels, bits, busy);
}

bool WaveOutSink::IsOpen() const { return backend_->IsOpen(); }

bool WaveOutSink::SamplePosition(uint32_t& samples, uint32_t& generation) const {
    return backend_->SamplePosition(samples, generation);
}

void WaveOutSink::Play(uint32_t slot, const uint8_t* bytes, uint32_t length) {
    backend_->Play(slot, bytes, length);
}

void WaveOutSink::Reset() { backend_->Reset(); }

}
