#include "wave_in_backend.h"

namespace cerfcore {

void WaveInBackend::Start(const char* log_tag, size_t cap_bytes) {
    log_tag_ = log_tag;
    fifo_.SetCap(cap_bytes);
    Begin();
}

void WaveInBackend::Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
    if (request_.Set(sample_rate_hz, channels, bits)) OnRequest();
}

void WaveInBackend::Clear() { fifo_.Clear(); }

size_t WaveInBackend::Take(uint8_t* dst, size_t bytes) { return fifo_.Take(dst, bytes); }

}
