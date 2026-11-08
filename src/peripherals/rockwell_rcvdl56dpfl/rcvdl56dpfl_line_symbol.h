#pragma once

#include <array>
#include <cstdint>

namespace cerfcore {

struct Rcvdl56dpflSymbol {
    enum class Kind : uint8_t { kChar, kFlag, kAbort, kMarkByte, kPattern, kBits };
    Kind    kind  = Kind::kChar;
    uint8_t value = 0u;
    uint8_t extra = 0u;
};

struct Rcvdl56dpflTxOut {
    std::array<Rcvdl56dpflSymbol, 4> sent{};
    uint8_t  sent_count     = 0u;
    uint64_t tick           = 0u;
    bool     underrun_abort = false;
    uint8_t  pattern_done   = 0u;

    void Send(Rcvdl56dpflSymbol::Kind kind, uint8_t value = 0u, uint8_t extra = 0u) {
        sent[sent_count++] = Rcvdl56dpflSymbol{kind, value, extra};
    }
};

}
