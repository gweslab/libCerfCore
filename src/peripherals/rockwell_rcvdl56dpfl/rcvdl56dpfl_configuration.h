#pragma once

#include "rcvdl56dpfl_line_config.h"

#include <array>
#include <cstdint>

namespace cerfcore {

class Rcvdl56dpflDspRam;

struct Rcvdl56dpflCleardown {
    bool    documented = false;
    uint8_t conf       = 0u;
    bool    abort_code = false;
};

class Rcvdl56dpflConfiguration {
public:
    static constexpr uint32_t kRegisterCount = 32u;
    using Registers = std::array<uint8_t, kRegisterCount>;

    static uint32_t BitRate(uint8_t conf);
    static bool     Fax(uint8_t conf);
    static bool     FskDataState(uint8_t conf);
    static bool     V22Family(uint8_t conf);
    static bool     Retrains(uint8_t conf);
    static bool     RenegotiatesRate(uint8_t conf);
    static bool     V8OrV34(uint8_t conf);
    static bool     ReportsV25AnswerTone(uint8_t conf);
    static bool     SameRetrainMode(uint8_t a, uint8_t b);
    static Rcvdl56dpflCleardown CleardownOf(uint8_t conf);
    static Rcvdl56dpflCleardown V8Cleardown();
    static uint8_t  SpeedCode(uint32_t bit_rate);
    static uint8_t  UnmodeledBits(uint32_t reg);
    static bool     RemoteLoopRequested(const Registers& regs, uint8_t conf);
    static Rcvdl56dpflLineConfig Line(const Registers& regs, const Rcvdl56dpflDspRam& ram, uint8_t conf,
                                      bool internal_loop, bool v21_sync);
};

}
