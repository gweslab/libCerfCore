#pragma once

#include "capped_byte_fifo.h"
#include "wave_in_request.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace cerfcore {

class InstanceThreads;

class WaveInBackend {
public:
    static std::unique_ptr<WaveInBackend> Create();

    virtual ~WaveInBackend() = default;

    void         Start(InstanceThreads& threads, const char* log_tag, size_t cap_bytes);
    virtual void Stop() = 0;

    void   Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);
    void   Clear();
    size_t Take(uint8_t* dst, size_t bytes);

protected:
    WaveInBackend() = default;

    virtual void Begin()     = 0;
    virtual void OnRequest() = 0;

    CappedByteFifo fifo_;
    WaveInRequest  request_;
    const char*      log_tag_ = "WaveInFifo";
    InstanceThreads* threads_ = nullptr;
};

}
