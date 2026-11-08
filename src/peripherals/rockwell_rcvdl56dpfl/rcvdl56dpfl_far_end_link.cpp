#include "rcvdl56dpfl_far_end_link.h"

#include "rcvdl56dpfl_bit_tick.h"
#include "rcvdl56dpfl_v54_pattern.h"
#include "../serial/uart_frame.h"

#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint8_t                kLineByteBits = 8u;
constexpr SerialLine::LineConfig kV14Format{};

}

void Rcvdl56dpflFarEndLink::Connect(uint64_t now) {
    Disconnect();
    connected_ = true;
    sync_due_  = now + kLineByteBits;
}

void Rcvdl56dpflFarEndLink::Disconnect() {
    connected_  = false;
    pending_.clear();
    async_next_ = 0u;
    async_last_ = 0u;
    sync_due_   = 0u;
    tx_bits_    = 0u;
    tx_count_   = 0u;
    guest_zero_insertion_.Reset();
    decode_     = Decode::kHunt;
    rx_value_   = 0u;
    rx_count_   = 0u;
    drained_    = false;
    outbound_.clear();
}

void Rcvdl56dpflFarEndLink::Queue(const Rcvdl56dpflLineConfig& c, const std::vector<uint8_t>& bytes, uint64_t now) {
    if (bytes.empty()) return;
    if (pending_.empty()) async_next_ = std::max(now, async_last_) + c.AsyncCharBits();
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
}

uint64_t Rcvdl56dpflFarEndLink::NextTick(const Rcvdl56dpflLineConfig& c, uint64_t now) const {
    if (!connected_) return kRcvdl56dpflNoEvent;
    if (!c.async) return std::max(sync_due_, now);
    if (!pending_.empty()) return async_next_;
    return kRcvdl56dpflNoEvent;
}

Rcvdl56dpflSymbol Rcvdl56dpflFarEndLink::Deliver(const Rcvdl56dpflLineConfig& c, uint64_t tick) {
    if (!c.async) {
        sync_due_ = tick + kLineByteBits;
        return Rcvdl56dpflSymbol{Rcvdl56dpflSymbol::Kind::kBits, NextLineByte()};
    }
    const uint8_t value = pending_.front();
    pending_.pop_front();
    async_last_ = async_next_;
    if (pending_.empty())
        drained_ = true;
    else
        async_next_ = async_last_ + c.AsyncCharBits();
    return Rcvdl56dpflSymbol{Rcvdl56dpflSymbol::Kind::kChar, value};
}

uint8_t Rcvdl56dpflFarEndLink::NextLineByte() {
    uint8_t byte = 0u;
    for (uint8_t i = 0; i < kLineByteBits; ++i) {
        if (tx_count_ == 0u && !pending_.empty()) {
            tx_bits_  = static_cast<uint16_t>(UartFrameLevels(kV14Format, pending_.front()));
            tx_count_ = static_cast<uint8_t>(UartFrameBits(kV14Format));
            pending_.pop_front();
        }
        uint8_t bit = 1u;
        if (tx_count_ != 0u) {
            bit = static_cast<uint8_t>(tx_bits_ & 1u);
            tx_bits_ = static_cast<uint16_t>(tx_bits_ >> 1);
            if (--tx_count_ == 0u && pending_.empty()) drained_ = true;
        }
        byte = static_cast<uint8_t>(byte | (bit << i));
    }
    return byte;
}

void Rcvdl56dpflFarEndLink::FromGuest(const Rcvdl56dpflLineConfig& c, const Rcvdl56dpflSymbol& s) {
    if (!connected_) return;
    switch (s.kind) {
        case Rcvdl56dpflSymbol::Kind::kChar:
            if (c.async)
                outbound_.push_back(s.value);
            else if (c.hdlc)
                guest_zero_insertion_.Byte(s.value, [this](uint8_t bit) { DecodeBit(bit); });
            else
                DecodeBits(s.value);
            return;
        case Rcvdl56dpflSymbol::Kind::kFlag:
            guest_zero_insertion_.Reset();
            DecodeBits(kRcvdl56dpflHdlcFlag);
            return;
        case Rcvdl56dpflSymbol::Kind::kAbort:
            guest_zero_insertion_.Reset();
            DecodeOnes(kLineByteBits);
            return;
        case Rcvdl56dpflSymbol::Kind::kMarkByte:
        case Rcvdl56dpflSymbol::Kind::kBits:
            guest_zero_insertion_.Reset();
            DecodeBits(s.value);
            return;
        case Rcvdl56dpflSymbol::Kind::kPattern:
            guest_zero_insertion_.Reset();
            DecodePattern(s.extra);
            return;
    }
}

void Rcvdl56dpflFarEndLink::DecodeBits(uint8_t bits) {
    for (uint8_t i = 0; i < kLineByteBits; ++i) DecodeBit(static_cast<uint8_t>((bits >> i) & 1u));
}

void Rcvdl56dpflFarEndLink::DecodeOnes(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) DecodeBit(1u);
}

void Rcvdl56dpflFarEndLink::DecodePattern(uint8_t pattern_bit) {
    const Rcvdl56dpflV54Pattern pattern = Rcvdl56dpflV54Pattern::Of(pattern_bit);
    Rcvdl56dpflV54Scrambler scrambler;
    for (uint32_t i = 0; i < pattern.scrambled_bits; ++i) DecodeBit(scrambler.Next(pattern.input));
    DecodeOnes(pattern.trailing_ones);
}

void Rcvdl56dpflFarEndLink::DecodeBit(uint8_t bit) {
    switch (decode_) {
        case Decode::kHunt:
            if (bit == 0u) {
                decode_   = Decode::kData;
                rx_value_ = 0u;
                rx_count_ = 0u;
            }
            return;
        case Decode::kData:
            rx_value_ = static_cast<uint8_t>(rx_value_ | (bit << rx_count_));
            if (++rx_count_ == kV14Format.data_bits) decode_ = Decode::kStop;
            return;
        case Decode::kStop:
            if (bit != 0u) {
                outbound_.push_back(rx_value_);
                decode_ = Decode::kHunt;
            } else {
                decode_ = Decode::kResync;
            }
            return;
        case Decode::kResync:
            if (bit != 0u) decode_ = Decode::kHunt;
            return;
    }
}

std::vector<uint8_t> Rcvdl56dpflFarEndLink::TakeOutbound() {
    std::vector<uint8_t> out;
    out.swap(outbound_);
    return out;
}

bool Rcvdl56dpflFarEndLink::TakeDrained() {
    const bool drained = drained_;
    drained_ = false;
    return drained;
}

bool Rcvdl56dpflFarEndLink::SameStateExceptDueTick(const Rcvdl56dpflFarEndLink& other) const {
    return connected_ == other.connected_ && pending_ == other.pending_ && async_next_ == other.async_next_ &&
           async_last_ == other.async_last_ && tx_bits_ == other.tx_bits_ && tx_count_ == other.tx_count_;
}

}
