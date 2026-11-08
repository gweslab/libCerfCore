#pragma once

#include "rcvdl56dpfl_bit_tick.h"
#include "rcvdl56dpfl_fifo.h"
#include "rcvdl56dpfl_frame_check.h"
#include "rcvdl56dpfl_hdlc_bits.h"
#include "rcvdl56dpfl_line_config.h"
#include "rcvdl56dpfl_line_symbol.h"

#include <array>
#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Rcvdl56dpflTransmitter {
public:
    void Reset();

    void CarrierOn(const Rcvdl56dpflLineConfig& c, uint64_t tick, Rcvdl56dpflTxOut* out);
    void CarrierOff();
    bool Carrier() const { return state_ != State::kOff; }

    bool Full(const Rcvdl56dpflLineConfig& c) const { return count_ >= Depth(c); }
    void Load(const Rcvdl56dpflLineConfig& c, uint8_t value, bool last, bool ninth);
    void Reconfigure(const Rcvdl56dpflLineConfig& c, uint64_t now, Rcvdl56dpflTxOut* out);
    void EventDue(const Rcvdl56dpflLineConfig& c, Rcvdl56dpflTxOut* out);
    uint64_t EventTick() const { return event_; }

    bool Tdbe(const Rcvdl56dpflLineConfig& c) const;
    bool Txfnf(const Rcvdl56dpflLineConfig& c) const { return count_ < Depth(c); }
    bool Txhf() const { return count_ >= kRcvdl56dpflFifoHalfFull; }
    bool SendingFlags(const Rcvdl56dpflLineConfig& c) const;
    bool SendingFcs() const { return state_ == State::kFcs; }
    bool Idling() const { return state_ == State::kIdle; }
    bool SameStateExceptEventTick(const Rcvdl56dpflTransmitter& other) const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    bool operator==(const Rcvdl56dpflTransmitter&) const = default;

    enum class State : uint8_t { kOff, kIdle, kData, kFcs, kEndFlag, kAbort, kPattern };
    enum class Idle : uint8_t { kFill, kHold, kPrbs };

    static uint16_t Depth(const Rcvdl56dpflLineConfig& c);
    Idle     IdleFor(const Rcvdl56dpflLineConfig& c) const;
    void     EnterIdle(const Rcvdl56dpflLineConfig& c, Idle idle, uint64_t tick, bool flag_done,
                       Rcvdl56dpflTxOut* out);
    void     ScheduleIdle(const Rcvdl56dpflLineConfig& c, uint64_t now);
    void     IdleBoundary(const Rcvdl56dpflLineConfig& c, uint64_t tick, Rcvdl56dpflTxOut* out);
    void     EndData(const Rcvdl56dpflLineConfig& c, uint64_t tick, Rcvdl56dpflTxOut* out);
    void     StartData(const Rcvdl56dpflLineConfig& c, uint64_t tick);
    void     StartPattern(const Rcvdl56dpflLineConfig& c, uint64_t tick);
    uint64_t DataStart(const Rcvdl56dpflLineConfig& c, uint64_t now) const;
    uint64_t NextBoundary(uint64_t now) const;
    uint8_t  ParityBit(const Rcvdl56dpflLineConfig& c, uint8_t value) const;
    uint32_t StuffedBits(uint8_t value);
    void     ClearFrame();

    State    state_ = State::kOff;
    Idle     idle_  = Idle::kHold;
    uint64_t anchor_ = 0u;
    uint64_t event_  = kRcvdl56dpflNoEvent;
    std::array<uint8_t, kRcvdl56dpflExtendedFifoDepth> fifo_{};
    std::array<uint8_t, kRcvdl56dpflExtendedFifoDepth> flags_{};
    uint16_t head_  = 0u;
    uint16_t count_ = 0u;
    uint8_t  shift_       = 0u;
    uint8_t  shift_flags_ = 0u;
    Rcvdl56dpflZeroInsertion zero_insertion_;
    uint8_t  in_frame_    = 0u;
    uint8_t  flag_seen_   = 0u;
    uint8_t  pattern_bit_ = 0u;
    Rcvdl56dpflFrameCheck fcs_;
};

}
