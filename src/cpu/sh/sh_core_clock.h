#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

namespace cerfcore {

class ShCoreClock : public Service {
public:
    using Service::Service;

    virtual GuestCycleClock::Rate ResetRate() const = 0;
};

}
