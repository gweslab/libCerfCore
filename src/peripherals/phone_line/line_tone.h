#pragma once

#include <cstdint>

namespace cerfcore {

struct LineTone {
    double  freq_hz;
    double  level_dbm;
    double  am_hz       = 0.0;
    double  am_depth    = 0.0;
    int64_t reversal_ns = 0;
    int64_t origin_ns   = 0;
};

}
