#pragma once

#include <cerfcore/core/callback_list.h>
#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class Pm8058Power;
class StateReader;
class StateWriter;

class Pm8058PowerKey : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetPressed(bool pressed);
    void ConfigureHardReset(uint32_t hold_ms, bool power_off);
    void AddListener(std::function<void(bool)> fn);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    void ApplyPressed(bool pressed);
    void OnHoldExpired();

    Pm8058Power*            power_ = nullptr;
    GuestCycleClock*        clock_ = nullptr;
    GuestCycleClock::Event* hold_  = nullptr;
    CallbackList<bool>      listeners_;
    bool                    pressed_        = false;
    bool                    user_wake_held_ = false;
    uint32_t                hard_reset_ms_  = 0u;
    bool                    hard_reset_off_ = false;
};

}
