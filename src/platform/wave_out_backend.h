#pragma once

#include "wave_out_reopen_policy.h"
#include "wave_out_sink.h"

#include <cstdint>
#include <memory>
#include <mutex>

namespace cerfcore {

class WaveOutBackend : protected WaveOutDevice {
public:
    using StartHandler   = WaveOutSink::StartHandler;
    using CommandHandler = WaveOutSink::CommandHandler;
    using DoneHandler    = WaveOutSink::DoneHandler;

    static std::unique_ptr<WaveOutBackend> Create();

    virtual ~WaveOutBackend() = default;

    void Start(Fatal& fatal, InstanceThreads& threads, StartHandler on_start, CommandHandler on_command,
               DoneHandler on_done, const char* log_tag);
    virtual void Stop()                          = 0;
    virtual void PostCommand(uint32_t command)   = 0;

    bool EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits, bool busy);
    bool IsOpen() const;
    bool SamplePosition(uint32_t& samples, uint32_t& generation) const;

    virtual void Play(uint32_t slot, const uint8_t* bytes, uint32_t length) = 0;
    virtual void Reset()                                                   = 0;

protected:
    WaveOutBackend() = default;

    virtual void Begin(StartHandler on_start, CommandHandler on_command, DoneHandler on_done) = 0;
    virtual bool DevicePositionLocked(uint32_t& samples) const                                = 0;

    mutable std::mutex  mtx_;
    WaveOutReopenPolicy policy_;
    uint32_t            generation_ = 0;
    Fatal*              fatal_      = nullptr;
    InstanceThreads*    threads_    = nullptr;
    const char*         log_tag_    = "WaveOutSink";
};

}
