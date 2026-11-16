#include "imx6_ipu_di_timing.h"

namespace cerfcore {

namespace imx6_ipu {

namespace {

constexpr uint32_t kGeneral    = 0x0000u >> 2;
constexpr uint32_t kBsClkgen0  = 0x0004u >> 2;
constexpr uint32_t kSwGen0     = 0x000Cu >> 2;
constexpr uint32_t kSwGen1     = 0x0030u >> 2;
constexpr uint32_t kSyncAsGen  = 0x0054u >> 2;
constexpr uint32_t kCounters   = 9u;

constexpr uint32_t kGeneralVsyncExt = 1u << 21;
constexpr uint32_t kGeneralClkExt   = 1u << 20;

constexpr uint32_t kRunValueShift = 19u;
constexpr uint32_t kRunValueMask  = 0xFFFu;
constexpr uint32_t kRunResShift   = 16u;
constexpr uint32_t kRunResMask    = 7u;
constexpr uint32_t kAutoReload    = 1u << 28;

uint32_t RunResolution(uint32_t sw_gen0) { return (sw_gen0 >> kRunResShift) & kRunResMask; }

}

bool DiTiming::ExternalClock() const { return (regs[kGeneral] & kGeneralClkExt) != 0u; }

uint32_t DiTiming::ClockPeriod16() const { return regs[kBsClkgen0] & 0xFFFu; }

uint64_t DiTiming::FrameClocks() const {
    return CounterClocks(((regs[kSyncAsGen] >> 13) & 7u) + 1u, 0u);
}

uint64_t DiTiming::VsyncPreClocks() const {
    const uint32_t vsync = ((regs[kSyncAsGen] >> 13) & 7u) + 1u;
    const uint32_t resolution = RunResolution(regs[kSwGen0 + vsync - 1u]);
    if (resolution < 2u || resolution > vsync) return 0u;
    const uint64_t line = CounterClocks(resolution - 1u, 0u);
    const uint64_t frame = FrameClocks();
    if (line == 0u || line > frame / 2u) return 0u;
    return frame - line * 2u;
}

uint64_t DiTiming::CounterClocks(uint32_t counter, uint32_t depth) const {
    const uint32_t sw_gen0 = regs[kSwGen0 + counter - 1u];
    const uint64_t run     = ((sw_gen0 >> kRunValueShift) & kRunValueMask) + 1u;
    const uint32_t res     = RunResolution(sw_gen0);
    if (res == 1u) return run;
    if (res < 2u || res > counter || depth >= kCounters) return 0u;
    const uint64_t source = CounterClocks(res - 1u, depth + 1u);
    return source == 0u ? 0u : run * source;
}

const char* DiTiming::Unmodelled() const {
    if ((regs[kGeneral] & kGeneralVsyncExt) != 0u) return "an external VSYNC (di_vsync_ext)";
    if (ClockPeriod16() < 16u) return "a display clock period below one DI clock";
    const uint32_t vsync = ((regs[kSyncAsGen] >> 13) & 7u) + 1u;
    if ((regs[kSwGen1 + vsync - 1u] & kAutoReload) == 0u) {
        return "a VSYNC counter without auto reload";
    }
    for (uint32_t c = 1u; c <= vsync; ++c) {
        const uint32_t res = RunResolution(regs[kSwGen0 + c - 1u]);
        if (res >= 5u) return "a counter triggered by a CSI or external VSYNC, or always on";
    }
    if (FrameClocks() == 0u) return "a VSYNC counter whose trigger chain is disabled";
    if (VsyncPreClocks() == 0u) return "a VSYNC counter without two programmed preceding lines";
    return nullptr;
}

}

}
