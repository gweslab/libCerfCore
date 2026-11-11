#pragma once

#include "../held_input_queue.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace cerfcore {

class StateWriter;
class StateReader;

struct Ps2MouseTraits {
    bool reset_wrap_disables_stream = false;
    bool applies_scaling            = true;
};

struct Ps2MouseSample {
    int32_t  x          = 0;
    int32_t  y          = 0;
    bool     overflow_x = false;
    bool     overflow_y = false;
    uint32_t buttons    = 0;
};

class Ps2Mouse {
public:
    static constexpr uint32_t kButtonLeft  = 0x1u;
    static constexpr uint32_t kButtonRight = 0x2u;

    using HaltFn = std::function<void(const char* what, unsigned value)>;

    Ps2Mouse(std::function<void()> on_data, HaltFn halt, Ps2MouseTraits traits = {});

    void    WriteCommand(uint8_t byte);
    bool    HasData() const;
    uint8_t ReadData();
    void    QueueMotion(int dx, int dy, uint32_t button_mask);
    void    Sample();
    bool    TakeSample(Ps2MouseSample& sample);
    uint8_t SampleRate() const;

    bool AwaitsCommand() const;
    void Acknowledge(std::vector<uint8_t> data = {});
    bool Reject();
    void AbortCommand();
    void StartStreamReporting();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);
    void Reset();

private:
    void                 CommandLocked(uint8_t b);
    void                 ParameterLocked(uint8_t b);
    void                 AcknowledgeLocked(std::vector<uint8_t> data);
    bool                 RejectLocked();
    void                 SetDefaultsLocked();
    void                 ZeroCountsLocked();
    void                 ResetCountersLocked();
    bool                 StreamingLocked() const;
    bool                 MovedLocked() const;
    void                 QueueReportLocked();
    bool                 TakeSampleLocked(Ps2MouseSample& sample);
    uint8_t              StatusLocked() const;
    uint8_t              ButtonBitsLocked() const;
    std::vector<uint8_t> AccumulatedPacketLocked() const;
    void                 RaiseData();

    mutable std::mutex    mtx_;
    std::function<void()> on_data_;
    HaltFn                halt_;
    Ps2MouseTraits        traits_;

    std::deque<uint8_t>  out_;
    std::vector<uint8_t> last_packet_;
    uint8_t              pending_command_ = 0;
    uint8_t              invalid_count_   = 0;
    bool                 reporting_       = false;
    bool                 remote_          = false;
    bool                 wrap_            = false;
    bool                 scaling_2to1_    = false;
    uint8_t              sample_rate_     = 100;
    uint8_t              resolution_      = 2;
    uint32_t             buttons_         = 0;
    uint32_t             reported_buttons_ = 0;
    HeldInputQueue<Ps2MouseSample> queued_;
    int32_t              count_x_         = 0;
    int32_t              count_y_         = 0;
    bool                 overflow_x_      = false;
    bool                 overflow_y_      = false;
};

}
