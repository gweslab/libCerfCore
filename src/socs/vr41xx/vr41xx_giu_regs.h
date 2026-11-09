#pragma once

#include <cstdint>

namespace cerfcore {

namespace cerf_vr41xx_giu_detail {

constexpr uint32_t kOffIoSelL    = 0x00u;
constexpr uint32_t kOffIoSelH    = 0x02u;
constexpr uint32_t kOffPiodL     = 0x04u;
constexpr uint32_t kOffPiodH     = 0x06u;
constexpr uint32_t kOffIntStatL  = 0x08u;
constexpr uint32_t kOffIntStatH  = 0x0Au;
constexpr uint32_t kOffIntEnL    = 0x0Cu;
constexpr uint32_t kOffIntEnH    = 0x0Eu;
constexpr uint32_t kOffIntTypL   = 0x10u;
constexpr uint32_t kOffIntTypH   = 0x12u;
constexpr uint32_t kOffIntAlSelL = 0x14u;
constexpr uint32_t kOffIntAlSelH = 0x16u;
constexpr uint32_t kOffIntHtSelL = 0x18u;
constexpr uint32_t kOffIntHtSelH = 0x1Au;
constexpr uint32_t kOffPoDatL    = 0x1Cu;
constexpr uint32_t kOffPoDatH    = 0x1Eu;

constexpr uint32_t kOffUseUpdn  = 0x00u;
constexpr uint32_t kOffTermUpdn = 0x02u;

constexpr uint16_t kIoSelLWritable = 0x7FFFu;

constexpr uint16_t kPoDatHWritable = 0x0303u;

}

}
