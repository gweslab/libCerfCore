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

class Rcvdl56dpflReceiver {
public:
    static constexpr uint8_t  kStatusSyncd   = 0x01u;
    static constexpr uint8_t  kStatusFe      = 0x10u;
    static constexpr uint8_t  kStatusPe      = 0x20u;
    static constexpr uint8_t  kStatusRxp     = 0x80u;

    void Reset();
    void LostCarrier();
    void Accept(const Rcvdl56dpflLineConfig& c, const Rcvdl56dpflSymbol& s, uint64_t tick, bool* overrun,
                bool* v54_detected);
    void HostRead(const Rcvdl56dpflLineConfig& c, uint64_t tick);
    void IdleTimeout();
    uint64_t EventTick() const { return idle_event_; }

    bool    Empty() const { return count_ == 0u; }
    bool    Rdbf() const { return rdbf_ != 0u; }
    bool    Rxhf() const { return count_ >= kRcvdl56dpflFifoHalfFull; }
    bool    FlagsDetected() const { return flags_detected_ != 0u; }
    uint8_t Front() const { return data_[head_]; }
    uint8_t FrontStatus() const { return status_[head_]; }

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

    bool operator==(const Rcvdl56dpflReceiver&) const = default;

private:
    void Push(const Rcvdl56dpflLineConfig& c, uint8_t value, uint8_t status, uint64_t tick, bool* overrun);
    void DeliverHeld(const Rcvdl56dpflLineConfig& c, uint8_t last_status, uint64_t tick, bool* overrun);
    void TakeFrameByte(const Rcvdl56dpflLineConfig& c, uint8_t value, uint64_t tick, bool* overrun);
    void FrameByte(const Rcvdl56dpflLineConfig& c, uint8_t value, uint64_t tick, bool* overrun);
    void FlagReceived(const Rcvdl56dpflLineConfig& c, uint64_t tick, bool* overrun);
    void AbortReceived(const Rcvdl56dpflLineConfig& c, uint64_t tick, bool* overrun);
    void DeframeBits(const Rcvdl56dpflLineConfig& c, uint8_t bits, uint64_t tick, bool* overrun);
    void CloseFrame();

    std::array<uint8_t, kRcvdl56dpflExtendedFifoDepth> data_{};
    std::array<uint8_t, kRcvdl56dpflExtendedFifoDepth> status_{};
    uint16_t head_  = 0u;
    uint16_t count_ = 0u;
    std::array<uint8_t, Rcvdl56dpflFrameCheck::kFcs32Bytes> held_{};
    uint8_t  held_count_     = 0u;
    Rcvdl56dpflFrameCheck fcs_;
    uint8_t  flags_detected_ = 0u;
    uint8_t  rdbf_           = 0u;
    uint64_t idle_event_     = kRcvdl56dpflNoEvent;
    Rcvdl56dpflHdlcDeframer deframer_;
};

}
