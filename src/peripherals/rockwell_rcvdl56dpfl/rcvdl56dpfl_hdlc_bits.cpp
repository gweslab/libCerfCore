#include "rcvdl56dpfl_hdlc_bits.h"

#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint8_t kDeletedZeroOnes = 5u;
constexpr uint8_t kFlagOnes        = 6u;
constexpr uint8_t kAbortOnes       = 7u;
constexpr uint8_t kByteBits        = 8u;

}

Rcvdl56dpflHdlcDeframer::Event Rcvdl56dpflHdlcDeframer::Bit(uint8_t bit, uint8_t* byte) {
    if (bit != 0u) {
        if (ones_ == kAbortOnes) return Event::kNone;
        if (++ones_ == kAbortOnes) {
            synced_ = 0u;
            return Event::kAbort;
        }
        return ones_ < kFlagOnes ? DataBit(1u, byte) : Event::kNone;
    }
    const uint8_t ones = ones_;
    ones_ = 0u;
    if (ones == kFlagOnes) {
        synced_ = 1u;
        value_  = 0u;
        count_  = 0u;
        return Event::kFlag;
    }
    if (ones == kDeletedZeroOnes || ones == kAbortOnes) return Event::kNone;
    return DataBit(0u, byte);
}

Rcvdl56dpflHdlcDeframer::Event Rcvdl56dpflHdlcDeframer::DataBit(uint8_t bit, uint8_t* byte) {
    if (synced_ == 0u) return Event::kNone;
    value_ = static_cast<uint8_t>(value_ | (bit << count_));
    if (++count_ < kByteBits) return Event::kNone;
    *byte  = value_;
    value_ = 0u;
    count_ = 0u;
    return Event::kByte;
}

void Rcvdl56dpflHdlcDeframer::SaveState(StateWriter& w) const {
    w.Write("rx_hdlc_ones", ones_);
    w.Write("rx_hdlc_synced", synced_);
    w.Write("rx_hdlc_value", value_);
    w.Write("rx_hdlc_count", count_);
}

void Rcvdl56dpflHdlcDeframer::RestoreState(StateReader& r) {
    r.Read("rx_hdlc_ones", ones_);
    r.Read("rx_hdlc_synced", synced_);
    r.Read("rx_hdlc_value", value_);
    r.Read("rx_hdlc_count", count_);
}

}
