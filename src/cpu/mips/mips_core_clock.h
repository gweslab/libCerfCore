#pragma once

#include <cstdint>
#include <functional>

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/core/callback_list.h>

namespace cerfcore {

class MipsCoreClock : public Service {
public:
    using Service::Service;

    virtual GuestCycleClock::Rate ResetRate() const = 0;
    virtual uint64_t CyclesPerCountTick() const;
    virtual uint64_t CyclesPerTclkCounterTick() const;
    virtual uint32_t ConfigEc() const;

    void RegisterCountRateListener(std::function<void()> fn);
    void CountRateRestored();

private:
    CallbackList<> count_rate_listeners_;
};

}
