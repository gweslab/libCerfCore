#pragma once

#include <cstdint>

namespace cerfcore {

struct Imx6BootDisplayClock {
    uint32_t pll_video;
    uint32_t pll_video_num;
    uint32_t pll_video_denom;
    uint32_t misc2_video_div;
};

bool EncodeImx6BootDisplayClock(uint64_t pixel_clock_hz, Imx6BootDisplayClock& out);

constexpr uint32_t kImx6Cs2cdrLdbDi0SelMask = 7u << 9;
constexpr uint32_t kImx6Cscmr2LdbDi0IpuDiv7 = 1u << 10;
constexpr uint32_t kImx6ChsccdrDi0ClkSelMask = 7u;
constexpr uint32_t kImx6ChsccdrDi0ClkSelLdbDi0 = 3u;

}
