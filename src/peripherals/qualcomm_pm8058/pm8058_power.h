#pragma once

#include <cerfcore/core/callback_list.h>
#include <cerfcore/core/service.h>

#include <atomic>
#include <functional>

namespace cerfcore {

class StateReader;
class StateWriter;

class Pm8058Power : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void PowerOff();
    void Restart();
    void RequestTurnOn();
    bool Off() const { return off_.load(std::memory_order_acquire); }
    void AddPowerOffListener(std::function<void()> fn);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    std::atomic<bool> off_{false};
    std::atomic<bool> turn_on_{false};
    CallbackList<>    power_off_listeners_;
};

}
