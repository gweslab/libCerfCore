#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/core/callback_list.h>

#include <cstdint>
#include <functional>

namespace cerfcore {

class Sa11xxGpioClockInput : public Service {
public:
    using Service::Service;

    virtual bool Frequency(uint32_t pin, GuestCycleClock::Rate& hz) const = 0;

    void RegisterChangeListener(std::function<void()> fn);

protected:
    void NotifyChange();

private:
    CallbackList<> listeners_;
};

}
