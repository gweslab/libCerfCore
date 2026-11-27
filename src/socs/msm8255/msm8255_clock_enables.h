#pragma once

#include <cerfcore/core/service.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

inline constexpr uint32_t kMsm8255ClockIdCount = 375u;

class Msm8255ClockEnables : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void Record(uint32_t clock, bool on);
    bool Enabled(uint32_t clock) const;
    void RequireRunning(uint32_t clock, const char* block, const char* action) const;

    using Listener = std::function<void(bool)>;
    void AddListener(uint32_t clock, Listener listener);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    static constexpr uint32_t kWordCount = (kMsm8255ClockIdCount + 31u) / 32u;

    struct Watch {
        uint32_t clock;
        Listener listener;
    };

    std::vector<Watch> watches_;

    std::atomic<uint32_t> on_[kWordCount] = {};
};

}
