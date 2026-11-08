#include "rcvdl56dpfl_receiver.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

void Rcvdl56dpflReceiver::Reset() {
    *this = Rcvdl56dpflReceiver{};
}

void Rcvdl56dpflReceiver::LostCarrier() {
    CloseFrame();
    flags_detected_ = 0u;
    deframer_.Reset();
}

void Rcvdl56dpflReceiver::CloseFrame() {
    held_count_ = 0u;
    fcs_.Clear();
}

void Rcvdl56dpflReceiver::TakeFrameByte(const Rcvdl56dpflLineConfig& c, uint8_t value, uint64_t tick, bool* overrun) {
    if (fcs_.Bytes() == 0u) fcs_.Start(c.fcs_bytes);
    fcs_.Update(value);
    if (held_count_ == fcs_.Bytes()) {
        Push(c, held_[0], 0u, tick, overrun);
        std::copy(held_.begin() + 1, held_.begin() + held_count_, held_.begin());
        --held_count_;
    }
    held_[held_count_++] = value;
}

void Rcvdl56dpflReceiver::Accept(const Rcvdl56dpflLineConfig& c, const Rcvdl56dpflSymbol& s, uint64_t tick,
                                 bool* overrun, bool* v54_detected) {
    switch (s.kind) {
        case Rcvdl56dpflSymbol::Kind::kChar:
            if (c.hdlc) {
                FrameByte(c, s.value, tick, overrun);
                return;
            }
            Push(c, s.value, (c.async && c.parity && c.data_bits == 8u && s.extra) ? kStatusRxp : 0u, tick, overrun);
            return;
        case Rcvdl56dpflSymbol::Kind::kMarkByte:
        case Rcvdl56dpflSymbol::Kind::kBits:
            if (c.hdlc) {
                DeframeBits(c, s.value, tick, overrun);
                return;
            }
            Push(c, s.value, 0u, tick, overrun);
            return;
        case Rcvdl56dpflSymbol::Kind::kFlag:
            if (!c.hdlc) return;
            FlagReceived(c, tick, overrun);
            return;
        case Rcvdl56dpflSymbol::Kind::kAbort:
            if (!c.hdlc) return;
            AbortReceived(c, tick, overrun);
            return;
        case Rcvdl56dpflSymbol::Kind::kPattern:
            if (c.hdlc) AbortReceived(c, tick, overrun);
            if (c.v54_detectors & (s.extra >> 2)) *v54_detected = true;
            return;
    }
}

void Rcvdl56dpflReceiver::FrameByte(const Rcvdl56dpflLineConfig& c, uint8_t value, uint64_t tick, bool* overrun) {
    TakeFrameByte(c, value, tick, overrun);
    flags_detected_ = 0u;
}

void Rcvdl56dpflReceiver::FlagReceived(const Rcvdl56dpflLineConfig& c, uint64_t tick, bool* overrun) {
    if (fcs_.Bytes() != 0u)
        DeliverHeld(c, static_cast<uint8_t>(kStatusSyncd | (fcs_.Good() ? 0u : kStatusPe)), tick, overrun);
    CloseFrame();
    flags_detected_ = 1u;
}

void Rcvdl56dpflReceiver::AbortReceived(const Rcvdl56dpflLineConfig& c, uint64_t tick, bool* overrun) {
    DeliverHeld(c, kStatusFe, tick, overrun);
    CloseFrame();
    flags_detected_ = 0u;
}

void Rcvdl56dpflReceiver::DeframeBits(const Rcvdl56dpflLineConfig& c, uint8_t bits, uint64_t tick, bool* overrun) {
    for (uint8_t i = 0; i < 8u; ++i) {
        uint8_t byte = 0u;
        switch (deframer_.Bit(static_cast<uint8_t>((bits >> i) & 1u), &byte)) {
            case Rcvdl56dpflHdlcDeframer::Event::kNone:
                break;
            case Rcvdl56dpflHdlcDeframer::Event::kByte:
                FrameByte(c, byte, tick, overrun);
                break;
            case Rcvdl56dpflHdlcDeframer::Event::kFlag:
                FlagReceived(c, tick, overrun);
                break;
            case Rcvdl56dpflHdlcDeframer::Event::kAbort:
                AbortReceived(c, tick, overrun);
                break;
        }
    }
}

void Rcvdl56dpflReceiver::HostRead(const Rcvdl56dpflLineConfig& c, uint64_t tick) {
    head_ = static_cast<uint16_t>((head_ + 1u) % kRcvdl56dpflExtendedFifoDepth);
    --count_;
    if (count_ == 0u) {
        rdbf_       = 0u;
        idle_event_ = kRcvdl56dpflNoEvent;
        return;
    }
    if (rdbf_ == 0u && c.rx_timeout) idle_event_ = tick + c.rx_timeout_bits;
}

void Rcvdl56dpflReceiver::IdleTimeout() {
    idle_event_ = kRcvdl56dpflNoEvent;
    rdbf_       = 1u;
}

void Rcvdl56dpflReceiver::Push(const Rcvdl56dpflLineConfig& c, uint8_t value, uint8_t status, uint64_t tick,
                               bool* overrun) {
    const uint16_t depth = c.rx_fifo_extended ? kRcvdl56dpflExtendedFifoDepth : kRcvdl56dpflFifoDepth;
    if (count_ >= depth) {
        const uint16_t tail = static_cast<uint16_t>((head_ + count_ - 1u) % kRcvdl56dpflExtendedFifoDepth);
        data_[tail]   = value;
        status_[tail] = status;
        *overrun      = true;
    } else {
        const uint16_t slot = static_cast<uint16_t>((head_ + count_) % kRcvdl56dpflExtendedFifoDepth);
        data_[slot]   = value;
        status_[slot] = status;
        ++count_;
    }
    if (count_ >= c.rx_trigger) rdbf_ = 1u;
    idle_event_ = (rdbf_ != 0u || !c.rx_timeout) ? kRcvdl56dpflNoEvent : tick + c.rx_timeout_bits;
}

void Rcvdl56dpflReceiver::DeliverHeld(const Rcvdl56dpflLineConfig& c, uint8_t last_status, uint64_t tick,
                                      bool* overrun) {
    for (uint8_t i = 0; i < held_count_; ++i)
        Push(c, held_[i], i + 1u == held_count_ ? last_status : 0u, tick, overrun);
    held_count_ = 0u;
}

void Rcvdl56dpflReceiver::SaveState(StateWriter& w) const {
    w.Write("rx_data", data_);
    w.Write("rx_status", status_);
    w.Write("rx_head", head_);
    w.Write("rx_count", count_);
    w.Write("rx_held", held_);
    w.Write("rx_held_count", held_count_);
    fcs_.SaveState(w, "rx_frame_fcs", "rx_frame_check");
    w.Write("rx_flags_detected", flags_detected_);
    w.Write("rx_rdbf", rdbf_);
    w.Write("rx_idle_event", idle_event_);
    deframer_.SaveState(w);
}

void Rcvdl56dpflReceiver::RestoreState(StateReader& r) {
    r.Read("rx_data", data_);
    r.Read("rx_status", status_);
    r.Read("rx_head", head_);
    r.Read("rx_count", count_);
    r.Read("rx_held", held_);
    r.Read("rx_held_count", held_count_);
    fcs_.RestoreState(r, "rx_frame_fcs", "rx_frame_check");
    r.Read("rx_flags_detected", flags_detected_);
    r.Read("rx_rdbf", rdbf_);
    r.Read("rx_idle_event", idle_event_);
    deframer_.RestoreState(r);
}

}
