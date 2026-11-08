#pragma once

#include "rcvdl56dpfl_hdlc_bits.h"
#include "rcvdl56dpfl_line_config.h"
#include "rcvdl56dpfl_line_symbol.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace cerfcore {

class Rcvdl56dpflFarEndLink {
public:
    void Connect(uint64_t now);
    void Disconnect();
    bool Connected() const { return connected_; }

    void                 Queue(const Rcvdl56dpflLineConfig& c, const std::vector<uint8_t>& bytes, uint64_t now);
    uint64_t             NextTick(const Rcvdl56dpflLineConfig& c, uint64_t now) const;
    Rcvdl56dpflSymbol    Deliver(const Rcvdl56dpflLineConfig& c, uint64_t tick);
    void                 FromGuest(const Rcvdl56dpflLineConfig& c, const Rcvdl56dpflSymbol& s);
    std::vector<uint8_t> TakeOutbound();
    bool                 TakeDrained();

    bool SameStateExceptDueTick(const Rcvdl56dpflFarEndLink& other) const;

private:
    enum class Decode : uint8_t { kHunt, kData, kStop, kResync };

    uint8_t NextLineByte();
    void    DecodeBits(uint8_t bits);
    void    DecodeOnes(uint32_t count);
    void    DecodePattern(uint8_t pattern_bit);
    void    DecodeBit(uint8_t bit);

    bool                     connected_  = false;
    std::deque<uint8_t>      pending_;
    uint64_t                 async_next_ = 0u;
    uint64_t                 async_last_ = 0u;
    uint64_t                 sync_due_   = 0u;
    uint16_t                 tx_bits_    = 0u;
    uint8_t                  tx_count_   = 0u;
    Rcvdl56dpflZeroInsertion guest_zero_insertion_;
    Decode                   decode_     = Decode::kHunt;
    uint8_t                  rx_value_   = 0u;
    uint8_t                  rx_count_   = 0u;
    bool                     drained_    = false;
    std::vector<uint8_t>     outbound_;
};

}
