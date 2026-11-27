#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class Msm8255ClockReset : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void RegisterListener(uint32_t clock, std::function<void()> fn);

    void Pulse(uint32_t clock);
    void Assert(uint32_t clock);
    void Deassert(uint32_t clock);

    void RequireReleased(uint32_t clock, uint32_t addr) const {
        if (!held_.empty()) RequireReleasedHeld(clock, addr);
    }

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    struct Listener {
        uint32_t              clock;
        std::function<void()> fn;
    };

    void ResetBlocks(uint32_t clock);
    bool IsHeld(uint32_t clock) const;
    void RequireReleasedHeld(uint32_t clock, uint32_t addr) const;

    std::vector<Listener> listeners_;
    std::vector<uint32_t> held_;
};

}
