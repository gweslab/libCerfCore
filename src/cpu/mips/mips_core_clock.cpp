#include "mips_core_clock.h"

#include <utility>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

uint64_t MipsCoreClock::CyclesPerCountTick() const {
    emu_.Get<Fatal>().Die("MipsCoreClock: CP0 Count read on a core clock with no Count divider");
}

uint64_t MipsCoreClock::CyclesPerTclkCounterTick() const {
    emu_.Get<Fatal>().Die("MipsCoreClock: RTC TClock counter on a core clock with no TClock divider");
}

uint32_t MipsCoreClock::ConfigEc() const {
    emu_.Get<Fatal>().Die("MipsCoreClock: Config EC read on a core clock with no clock-ratio strap");
}

void MipsCoreClock::RegisterCountRateListener(std::function<void()> fn) {
    count_rate_listeners_.Add(std::move(fn));
}

void MipsCoreClock::CountRateRestored() {
    count_rate_listeners_.Call();
}

}
