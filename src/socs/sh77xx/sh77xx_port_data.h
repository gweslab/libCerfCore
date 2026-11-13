#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/pin_drive.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace cerfcore {

class Sh77xxPortData : public Peripheral {
public:
    using Peripheral::Peripheral;

    virtual PinDrive Drive(uint32_t port, uint32_t pin) const = 0;

    void AddDriveListener(std::function<void()> listener) { listeners_.push_back(std::move(listener)); }

protected:
    void NotifyDriveChanged() {
        for (const std::function<void()>& listener : listeners_) listener();
    }

private:
    std::vector<std::function<void()>> listeners_;
};

}
