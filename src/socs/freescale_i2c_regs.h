#pragma once

#include <cstdint>

namespace cerfcore {

namespace cerf_freescale_i2c {

constexpr uint32_t kSize = 0x00004000u;

constexpr uint32_t kOffIadr = 0x00u;
constexpr uint32_t kOffIfdr = 0x04u;
constexpr uint32_t kOffI2cr = 0x08u;
constexpr uint32_t kOffI2sr = 0x0Cu;
constexpr uint32_t kOffI2dr = 0x10u;

constexpr uint16_t kI2crIen      = 0x80u;
constexpr uint16_t kI2crIien     = 0x40u;
constexpr uint16_t kI2crMsta     = 0x20u;
constexpr uint16_t kI2crMtx      = 0x10u;
constexpr uint16_t kI2crTxak     = 0x08u;
constexpr uint16_t kI2crRsta     = 0x04u;
constexpr uint16_t kI2crReadable = kI2crIen | kI2crIien | kI2crMsta | kI2crMtx | kI2crTxak;

constexpr uint16_t kI2srIcf  = 0x80u;
constexpr uint16_t kI2srIbb  = 0x20u;
constexpr uint16_t kI2srIal  = 0x10u;
constexpr uint16_t kI2srIif  = 0x02u;
constexpr uint16_t kI2srRxak = 0x01u;
constexpr uint16_t kI2srReset = kI2srIcf | kI2srRxak;

}

}
