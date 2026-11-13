#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/pin_drive.h"
#include <cerfcore/core/callback_list.h>

#include <cstdint>
#include <functional>
#include <utility>

namespace cerfcore {

class Sh77xxPortData : public Peripheral {
public:
    using Peripheral::Peripheral;

    virtual PinDrive Drive(uint32_t port, uint32_t pin) const = 0;

    void AddDriveListener(std::function<void()> listener) { listeners_.Add(std::move(listener)); }

protected:
    void NotifyDriveChanged() {
        listeners_.Call();
    }

private:
    CallbackList<> listeners_;
};

}
