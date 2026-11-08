#pragma once

#include "rcvdl56dpfl_frame_check.h"

#include <cstdint>

namespace cerfcore {

struct Rcvdl56dpflLineConfig {
    bool     hdlc              = false;
    uint8_t  fcs_bytes         = Rcvdl56dpflFrameCheck::kFcs16Bytes;
    bool     fcs_sent          = true;
    bool     async             = false;
    uint8_t  data_bits         = 8u;
    bool     parity            = false;
    uint8_t  parity_select     = 0u;
    uint8_t  stop_bits         = 1u;
    bool     tx_fifo           = false;
    bool     tx_fifo_extended  = false;
    bool     rx_fifo_extended  = false;
    bool     abort_on_underrun = false;
    bool     cts               = false;
    bool     parallel          = false;
    bool     takes_tbuffer     = false;
    bool     mark_hold         = false;
    bool     send_511          = false;
    uint8_t  v54_pattern       = 0u;
    uint8_t  v54_detectors     = 0u;
    bool     loop_listens      = false;
    bool     far_end_listens   = false;
    uint8_t  rx_trigger        = 1u;
    bool     rx_timeout        = false;
    uint8_t  rx_timeout_bits   = 9u;

    uint32_t AsyncCharBits() const { return 1u + data_bits + (parity ? 1u : 0u) + stop_bits; }
};

}
