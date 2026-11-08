#include "rcvdl56dpfl_transmitter.h"

#include "rcvdl56dpfl_v54_pattern.h"
#include "../serial/uart_frame.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t kCharBits        = 8u;
constexpr uint32_t kFlagBits        = 8u;
constexpr uint32_t kAbortBits       = 8u;
constexpr uint8_t  kEntryLast       = 0x01u;
constexpr uint8_t  kEntryNinth      = 0x02u;
constexpr uint8_t  kParityStuff = 0u;
constexpr uint8_t  kParitySpace = 1u;
constexpr uint8_t  kParityEven  = 2u;

}

void Rcvdl56dpflTransmitter::Reset() {
    *this = Rcvdl56dpflTransmitter{};
}

uint16_t Rcvdl56dpflTransmitter::Depth(const Rcvdl56dpflLineConfig& c) {
    if (!c.tx_fifo) return 1u;
    return c.tx_fifo_extended ? kRcvdl56dpflExtendedFifoDepth : kRcvdl56dpflFifoDepth;
}

bool Rcvdl56dpflTransmitter::Tdbe(const Rcvdl56dpflLineConfig& c) const {
    if (c.tx_fifo && c.tx_fifo_extended) return count_ < Depth(c);
    return count_ == 0u;
}

bool Rcvdl56dpflTransmitter::SendingFlags(const Rcvdl56dpflLineConfig& c) const {
    if (c.hdlc) return state_ == State::kEndFlag || (state_ == State::kIdle && idle_ == Idle::kFill);
    return c.async && c.parallel && state_ == State::kIdle;
}

void Rcvdl56dpflTransmitter::Load(const Rcvdl56dpflLineConfig& c, uint8_t value, bool last, bool ninth) {
    const uint8_t flags = static_cast<uint8_t>((last ? kEntryLast : 0u) | (ninth ? kEntryNinth : 0u));
    if (!c.tx_fifo && count_ != 0u) {
        fifo_[head_]  = value;
        flags_[head_] = flags;
        return;
    }
    const uint16_t tail = static_cast<uint16_t>((head_ + count_) % kRcvdl56dpflExtendedFifoDepth);
    fifo_[tail]  = value;
    flags_[tail] = flags;
    ++count_;
}

void Rcvdl56dpflTransmitter::CarrierOn(const Rcvdl56dpflLineConfig& c, uint64_t tick, Rcvdl56dpflTxOut* out) {
    out->tick = tick;
    state_    = State::kIdle;
    idle_     = Idle::kHold;
    EnterIdle(c, IdleFor(c), tick, false, out);
}

void Rcvdl56dpflTransmitter::CarrierOff() {
    state_ = State::kOff;
    event_ = kRcvdl56dpflNoEvent;
    ClearFrame();
}

void Rcvdl56dpflTransmitter::Reconfigure(const Rcvdl56dpflLineConfig& c, uint64_t now, Rcvdl56dpflTxOut* out) {
    out->tick = now;
    const bool framing = state_ == State::kData || state_ == State::kFcs || state_ == State::kEndFlag;
    if (c.hdlc && c.mark_hold && framing) {
        EnterIdle(c, Idle::kHold, now, false, out);
        return;
    }
    if (state_ != State::kIdle) return;
    const Idle want = IdleFor(c);
    if (want != idle_) {
        EnterIdle(c, want, now, false, out);
        return;
    }
    ScheduleIdle(c, now);
}

void Rcvdl56dpflTransmitter::EventDue(const Rcvdl56dpflLineConfig& c, Rcvdl56dpflTxOut* out) {
    const uint64_t tick = event_;
    out->tick = tick;
    switch (state_) {
        case State::kOff:
            return;
        case State::kIdle:
            IdleBoundary(c, tick, out);
            return;
        case State::kData:
            EndData(c, tick, out);
            return;
        case State::kFcs: {
            for (uint8_t i = 0; i < fcs_.Bytes(); ++i) out->Send(Rcvdl56dpflSymbol::Kind::kChar, fcs_.FcsByte(i));
            ClearFrame();
            state_ = State::kEndFlag;
            event_ = tick + kFlagBits;
            return;
        }
        case State::kEndFlag:
            out->Send(Rcvdl56dpflSymbol::Kind::kFlag);
            if (c.takes_tbuffer && count_ != 0u && !(c.v54_pattern && c.cts)) {
                StartData(c, tick);
                return;
            }
            EnterIdle(c, IdleFor(c), tick, true, out);
            return;
        case State::kAbort:
            out->Send(Rcvdl56dpflSymbol::Kind::kAbort);
            EnterIdle(c, IdleFor(c), tick, false, out);
            return;
        case State::kPattern: {
            out->Send(Rcvdl56dpflSymbol::Kind::kPattern, 0u, pattern_bit_);
            out->pattern_done = pattern_bit_;
            Rcvdl56dpflLineConfig next = c;
            next.v54_pattern = static_cast<uint8_t>(next.v54_pattern & ~pattern_bit_);
            pattern_bit_ = 0u;
            EnterIdle(next, IdleFor(next), tick, false, out);
            return;
        }
    }
}

Rcvdl56dpflTransmitter::Idle Rcvdl56dpflTransmitter::IdleFor(const Rcvdl56dpflLineConfig& c) const {
    if (c.send_511 && !c.async) return Idle::kPrbs;
    return c.takes_tbuffer ? Idle::kFill : Idle::kHold;
}

void Rcvdl56dpflTransmitter::EnterIdle(const Rcvdl56dpflLineConfig& c, Idle idle, uint64_t tick, bool flag_done,
                                       Rcvdl56dpflTxOut* out) {
    const bool sending_ones = state_ == State::kIdle && idle_ != Idle::kFill;
    if (c.hdlc && idle != Idle::kFill && !sending_ones) out->Send(Rcvdl56dpflSymbol::Kind::kAbort);
    state_     = State::kIdle;
    idle_      = idle;
    anchor_    = tick;
    flag_seen_ = flag_done ? 1u : 0u;
    ClearFrame();
    ScheduleIdle(c, tick);
}

void Rcvdl56dpflTransmitter::ScheduleIdle(const Rcvdl56dpflLineConfig& c, uint64_t now) {
    event_ = kRcvdl56dpflNoEvent;
    if (c.v54_pattern && c.cts) {
        StartPattern(c, now);
        return;
    }
    if (idle_ == Idle::kFill && count_ != 0u) {
        const uint64_t start = DataStart(c, now);
        if (start == now) {
            StartData(c, now);
            return;
        }
        event_ = start;
        return;
    }
    if (!c.loop_listens && !c.far_end_listens) return;
    if (c.hdlc) {
        const bool every_boundary = c.far_end_listens && idle_ != Idle::kPrbs;
        if (every_boundary || (idle_ == Idle::kFill && flag_seen_ == 0u)) event_ = NextBoundary(now);
        return;
    }
    if (!c.async && idle_ != Idle::kPrbs) event_ = NextBoundary(now);
}

void Rcvdl56dpflTransmitter::IdleBoundary(const Rcvdl56dpflLineConfig& c, uint64_t tick, Rcvdl56dpflTxOut* out) {
    if (c.hdlc) {
        if (idle_ == Idle::kFill) {
            out->Send(Rcvdl56dpflSymbol::Kind::kFlag);
            flag_seen_ = 1u;
        } else if (idle_ == Idle::kHold) {
            out->Send(Rcvdl56dpflSymbol::Kind::kMarkByte, 0xFFu);
        }
    } else if (!c.async && idle_ != Idle::kPrbs) {
        out->Send(Rcvdl56dpflSymbol::Kind::kMarkByte, 0xFFu);
    }
    ScheduleIdle(c, tick);
}

void Rcvdl56dpflTransmitter::EndData(const Rcvdl56dpflLineConfig& c, uint64_t tick, Rcvdl56dpflTxOut* out) {
    if (c.async) {
        const uint8_t value = static_cast<uint8_t>(shift_ & ((1u << c.data_bits) - 1u));
        out->Send(Rcvdl56dpflSymbol::Kind::kChar, value, ParityBit(c, value));
    } else {
        out->Send(Rcvdl56dpflSymbol::Kind::kChar, shift_);
    }
    if (c.v54_pattern && c.cts) {
        StartPattern(c, tick);
        return;
    }
    if (!c.hdlc) {
        if (c.takes_tbuffer && count_ != 0u) {
            StartData(c, tick);
            return;
        }
        EnterIdle(c, IdleFor(c), tick, false, out);
        return;
    }
    const bool last = c.tx_fifo && (shift_flags_ & kEntryLast) != 0u;
    if (!c.takes_tbuffer) {
        EnterIdle(c, IdleFor(c), tick, false, out);
        return;
    }
    if (!last && count_ != 0u) {
        StartData(c, tick);
        return;
    }
    if (c.tx_fifo && !last && c.abort_on_underrun) {
        out->underrun_abort = true;
        ClearFrame();
        state_ = State::kAbort;
        event_ = tick + kAbortBits;
        return;
    }
    if (fcs_.Bytes() == 0u) {
        ClearFrame();
        state_ = State::kEndFlag;
        event_ = tick + kFlagBits;
        return;
    }
    uint32_t bits = 0u;
    for (uint8_t i = 0; i < fcs_.Bytes(); ++i) bits += StuffedBits(fcs_.FcsByte(i));
    state_ = State::kFcs;
    event_ = tick + bits;
}

void Rcvdl56dpflTransmitter::StartData(const Rcvdl56dpflLineConfig& c, uint64_t tick) {
    shift_       = fifo_[head_];
    shift_flags_ = flags_[head_];
    head_        = static_cast<uint16_t>((head_ + 1u) % kRcvdl56dpflExtendedFifoDepth);
    --count_;
    state_ = State::kData;
    uint32_t bits = kCharBits;
    if (c.hdlc) {
        if (in_frame_ == 0u) {
            in_frame_ = 1u;
            fcs_.Start(c.fcs_sent ? c.fcs_bytes : 0u);
        }
        fcs_.Update(shift_);
        bits = StuffedBits(shift_);
    } else if (c.async) {
        bits = c.AsyncCharBits();
    }
    event_ = tick + bits;
}

void Rcvdl56dpflTransmitter::StartPattern(const Rcvdl56dpflLineConfig& c, uint64_t tick) {
    pattern_bit_ = static_cast<uint8_t>(c.v54_pattern & (0u - c.v54_pattern));
    ClearFrame();
    state_ = State::kPattern;
    event_ = tick + Rcvdl56dpflV54Pattern::Of(pattern_bit_).Bits();
}

uint64_t Rcvdl56dpflTransmitter::DataStart(const Rcvdl56dpflLineConfig& c, uint64_t now) const {
    if (c.async) return now;
    const uint64_t since = now - anchor_;
    uint64_t chars = (since + kFlagBits - 1u) / kFlagBits;
    if (c.hdlc && chars == 0u) chars = 1u;
    return anchor_ + chars * kFlagBits;
}

uint64_t Rcvdl56dpflTransmitter::NextBoundary(uint64_t now) const {
    const uint64_t since = now - anchor_;
    return anchor_ + (since / kFlagBits + 1u) * kFlagBits;
}

uint8_t Rcvdl56dpflTransmitter::ParityBit(const Rcvdl56dpflLineConfig& c, uint8_t value) const {
    if (!c.parity) return 0u;
    if (c.parity_select == kParityStuff) return (shift_flags_ & kEntryNinth) ? 1u : 0u;
    const SerialLine::LineConfig::Parity parity =
        c.parity_select == kParitySpace  ? SerialLine::LineConfig::Parity::Space
        : c.parity_select == kParityEven ? SerialLine::LineConfig::Parity::Even
                                         : SerialLine::LineConfig::Parity::Odd;
    return UartParityBit(parity, value, c.data_bits) ? 1u : 0u;
}

uint32_t Rcvdl56dpflTransmitter::StuffedBits(uint8_t value) {
    uint32_t bits = 0u;
    zero_insertion_.Byte(value, [&bits](uint8_t) { ++bits; });
    return bits;
}

bool Rcvdl56dpflTransmitter::SameStateExceptEventTick(const Rcvdl56dpflTransmitter& other) const {
    Rcvdl56dpflTransmitter aligned = other;
    aligned.event_ = event_;
    return aligned == *this;
}

void Rcvdl56dpflTransmitter::ClearFrame() {
    in_frame_ = 0u;
    zero_insertion_.Reset();
    fcs_.Clear();
}

void Rcvdl56dpflTransmitter::SaveState(StateWriter& w) const {
    w.Write<uint8_t>("tx_state", static_cast<uint8_t>(state_));
    w.Write<uint8_t>("tx_idle", static_cast<uint8_t>(idle_));
    w.Write("tx_anchor", anchor_);
    w.Write("tx_event", event_);
    w.Write("tx_fifo", fifo_);
    w.Write("tx_fifo_flags", flags_);
    w.Write("tx_head", head_);
    w.Write("tx_count", count_);
    w.Write("tx_shift", shift_);
    w.Write("tx_shift_flags", shift_flags_);
    w.Write("tx_ones_run", zero_insertion_.Ones());
    w.Write("tx_in_frame", in_frame_);
    w.Write("tx_flag_seen", flag_seen_);
    w.Write("tx_pattern_bit", pattern_bit_);
    fcs_.SaveState(w, "tx_frame_fcs", "tx_frame_check");
}

void Rcvdl56dpflTransmitter::RestoreState(StateReader& r) {
    uint8_t state = 0u, idle = 0u;
    r.Read("tx_state", state);
    r.Read("tx_idle", idle);
    state_ = static_cast<State>(state);
    idle_  = static_cast<Idle>(idle);
    r.Read("tx_anchor", anchor_);
    r.Read("tx_event", event_);
    r.Read("tx_fifo", fifo_);
    r.Read("tx_fifo_flags", flags_);
    r.Read("tx_head", head_);
    r.Read("tx_count", count_);
    r.Read("tx_shift", shift_);
    r.Read("tx_shift_flags", shift_flags_);
    r.Read("tx_ones_run", zero_insertion_.Ones());
    r.Read("tx_in_frame", in_frame_);
    r.Read("tx_flag_seen", flag_seen_);
    r.Read("tx_pattern_bit", pattern_bit_);
    fcs_.RestoreState(r, "tx_frame_fcs", "tx_frame_check");
}

}
