#include "imx6_boot_display_clock.h"
#include "imx6_anatop.h"

namespace cerfcore {

namespace {

constexpr uint64_t kLdbSerialPerPixel = 7u;

constexpr uint32_t kDivSelectMin = 27u;
constexpr uint32_t kDivSelectMax = 54u;

constexpr uint64_t kVcoMinHz = 650000000u;
constexpr uint64_t kVcoMaxHz = 1300000000u;


struct PostDivider { uint64_t total; uint32_t post_div_select; uint32_t video_div; };

constexpr PostDivider kPostDividers[] = {
    {1u, 2u, 0u}, {2u, 1u, 0u}, {4u, 0u, 0u}, {8u, 0u, 1u}, {16u, 0u, 3u},
};

}

bool EncodeImx6BootDisplayClock(uint64_t pixel_clock_hz, Imx6BootDisplayClock& out) {
    const uint64_t serial_hz = pixel_clock_hz * kLdbSerialPerPixel;
    for (const PostDivider& d : kPostDividers) {
        const uint64_t vco_hz = serial_hz * d.total;
        if (vco_hz <= kVcoMinHz || vco_hz >= kVcoMaxHz) continue;
        const uint32_t div_select = static_cast<uint32_t>(vco_hz / kImx6RefHz);
        if (div_select < kDivSelectMin || div_select > kDivSelectMax) return false;
        out.pll_video       = kImx6PllLock | kImx6PllEnable | (d.post_div_select << 19) | div_select;
        out.pll_video_num   = static_cast<uint32_t>(vco_hz % kImx6RefHz);
        out.pll_video_denom = static_cast<uint32_t>(kImx6RefHz);
        out.misc2_video_div = d.video_div;
        return true;
    }
    return false;
}

}
