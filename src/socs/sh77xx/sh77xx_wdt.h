#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

namespace cerfcore {

class Sh77xxWdt : public Peripheral {
public:
    using Peripheral::Peripheral;

    virtual void CountAfterStandby() = 0;
    virtual void StopStandbyCount()  = 0;
};

}
