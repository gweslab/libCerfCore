#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/pin_drive.h"

#include <cstdint>

namespace cerfcore {

class Sh77xxPortData : public Peripheral {
public:
    using Peripheral::Peripheral;

    virtual PinDrive Drive(uint32_t port, uint32_t pin) const = 0;
};

}
