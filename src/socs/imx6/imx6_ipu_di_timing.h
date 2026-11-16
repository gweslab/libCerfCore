#pragma once

#include <cstdint>

namespace cerfcore {

namespace imx6_ipu {

struct DiTiming {
    const uint32_t* regs;

    bool     ExternalClock() const;
    uint32_t ClockPeriod16() const;
    uint64_t FrameClocks() const;
    uint64_t VsyncPreClocks() const;
    const char* Unmodelled() const;

private:
    uint64_t CounterClocks(uint32_t counter, uint32_t depth) const;
};

}

}
