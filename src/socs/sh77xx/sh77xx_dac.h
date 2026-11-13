#pragma once

#include <cerfcore/core/callback_list.h>
#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>
#include <functional>
#include <utility>

namespace cerfcore {

class Sh77xxDac : public Peripheral {
public:
    using Peripheral::Peripheral;

    struct Output {
        bool    enabled = false;
        uint8_t value   = 0u;
    };

    virtual Output ChannelOutput(uint32_t channel) const = 0;

    void AddOutputListener(std::function<void()> listener) { output_listeners_.Add(std::move(listener)); }
    void AddRestoreListener(std::function<void()> listener) { restore_listeners_.Add(std::move(listener)); }

protected:
    void NotifyOutputChanged() { output_listeners_.Call(); }
    void NotifyRestored() { restore_listeners_.Call(); }

private:
    CallbackList<> output_listeners_;
    CallbackList<> restore_listeners_;
};

}
