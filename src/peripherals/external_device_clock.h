#pragma once

#include <cerfcore/core/service.h>
#include "../execution/guest_cycle_clock.h"
#include "../socs/oscillator_ticks.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class ExternalDeviceClock : public Service {
public:
    class Deadline {
    private:
        friend class ExternalDeviceClock;

        uint64_t due_   = 0;
        bool     armed_ = false;
    };

    class Timer : private Deadline {
    public:
        explicit Timer(std::function<void()> on_due) : on_due_(std::move(on_due)) {}

    private:
        friend class ExternalDeviceClock;

        std::function<void()>   on_due_;
        GuestCycleClock::Event* event_ = nullptr;
    };

    using Service::Service;

    void OnReady() override;

    int64_t NowNs();
    Timer&  AddTimer(std::function<void()> on_due);
    void    Arm(Timer& timer, int64_t delay_ns);
    void    Disarm(Timer& timer);

    void SetDeadline(Deadline& deadline, int64_t delay_ns);
    void ClearDeadline(Deadline& deadline);
    bool Pending(const Deadline& deadline);
    void SaveDeadline(const Deadline& deadline, StateWriter& w, const char* prefix);
    void RestoreDeadline(Deadline& deadline, StateReader& r, const char* prefix);

    void SaveTimer(Timer& timer, StateWriter& w, const char* prefix);
    void RestoreTimer(Timer& timer, StateReader& r, const char* prefix);
    void ResumeTimer(Timer& timer);

private:
    void Check(Timer& timer);
    void ArmEvent(Timer& timer);
    void CheckAll();
    void RearmAll();

    OscillatorTicks                     ticks_{emu_, true};
    GuestCycleClock*                    clock_ = nullptr;
    std::vector<std::unique_ptr<Timer>> timers_;
};

}
