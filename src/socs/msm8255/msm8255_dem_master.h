#pragma once

#include <cerfcore/core/service.h>

#include "../../execution/guest_cycle_clock.h"

#include "msm8255_vic.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace cerfcore {

class Msm8255Acc;
class Msm8255Saw;
class StateReader;
class StateWriter;

class Msm8255DemMaster : public Service {
public:
    static constexpr uint32_t kPortNameBytes = 20u;
    using PortName = std::array<uint8_t, kPortNameBytes>;

    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Answer(uint32_t smsm_state_pa);
    void WakeSourceRaised(uint32_t source);
    void WakeForRpc(uint32_t prog, uint32_t proc, const PortName& port);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    enum class Episode : uint32_t { None = 0u, PowerCollapse = 1u, Sleep = 2u };

    void     FollowSlave(uint32_t slave);
    void     MonitorEpisodeSources();
    void     PostRaisedSources();
    void     TakeRaisedSources();
    void     Deliver(uint32_t reason, uint32_t pending_irqs);
    uint32_t SlaveDataPa();
    void     WakeOnSleepTimer();

    Msm8255Acc*             acc_         = nullptr;
    Msm8255Saw*             saw_         = nullptr;
    GuestCycleClock*        clock_       = nullptr;
    GuestCycleClock::Event* wake_event_  = nullptr;
    Episode                 episode_     = Episode::None;
    bool                    wake_armed_  = false;
    bool                    woken_       = false;
    int64_t                 wake_due_ns_ = 0;
    int64_t                 restored_remaining_ns_ = 0;
    uint32_t                wake_mask_   = 0u;
    Msm8255Vic::SourceSet   monitored_{};
    std::array<std::atomic<uint32_t>, Msm8255Vic::kBankCount> raised_{};
    std::atomic<bool>       raise_posted_{false};
};

}
