#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/core/callback_list.h>

#include <functional>

namespace cerfcore {

class Vr41xxHsp : public Peripheral {
public:
    using Peripheral::Peripheral;

    void RegisterAferstListener(std::function<void(bool high)> fn);

protected:
    void DriveAferst(bool high);

private:
    CallbackList<bool> aferst_listeners_;
    bool aferst_high_ = false;
};

}
