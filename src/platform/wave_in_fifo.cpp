#include "wave_in_fifo.h"

#include "wave_in_backend.h"

namespace cerfcore {

WaveInFifo::WaveInFifo() : backend_(WaveInBackend::Create()) {}

WaveInFifo::~WaveInFifo() { backend_->Stop(); }

void WaveInFifo::Start(const char* log_tag, size_t cap_bytes) { backend_->Start(log_tag, cap_bytes); }

void WaveInFifo::Stop() { backend_->Stop(); }

void WaveInFifo::Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
    backend_->Request(sample_rate_hz, channels, bits);
}

void WaveInFifo::Clear() { backend_->Clear(); }

size_t WaveInFifo::Take(uint8_t* dst, size_t bytes) { return backend_->Take(dst, bytes); }

}
