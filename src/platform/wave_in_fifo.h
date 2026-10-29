#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace cerfcore {

class InstanceThreads;
class WaveInBackend;

class WaveInFifo {
public:
    WaveInFifo();
    ~WaveInFifo();
    WaveInFifo(const WaveInFifo&)            = delete;
    WaveInFifo& operator=(const WaveInFifo&) = delete;

    void   Start(InstanceThreads& threads, const char* log_tag, size_t cap_bytes);
    void   Stop();

    void   Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);
    void   Clear();
    size_t Take(uint8_t* dst, size_t bytes);

private:
    std::unique_ptr<WaveInBackend> backend_;
};

}
