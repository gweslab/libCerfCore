#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

namespace cerfcore {

class Sh77xxCpg : public Service {
public:
    using Service::Service;

    virtual GuestCycleClock::Rate ResetCpuRate() const = 0;
    virtual GuestCycleClock::Rate CpuRate(uint16_t frqcr) const = 0;
    virtual GuestCycleClock::Rate PeripheralRate(uint16_t frqcr) const = 0;
    virtual uint8_t               Pll1Multiplier(uint16_t frqcr) const = 0;
};

}
