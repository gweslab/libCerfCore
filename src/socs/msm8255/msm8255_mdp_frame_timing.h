#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../oscillator_ticks.h"
#include "msm8255_mdp_pixel.h"

#include <array>
#include <cstdint>
#include <functional>

namespace cerfcore {

class StateReader;
class StateWriter;

inline constexpr uint32_t kMsm8255MdpSyncPrimary   = 0u;
inline constexpr uint32_t kMsm8255MdpSyncSecondary = 1u;

class Msm8255MdpFrameTiming : public Service {
public:
    explicit Msm8255MdpFrameTiming(CerfEmulator& emu) : Service(emu), vsync_(emu, false) {}

    using Completion = std::function<void(uint32_t)>;

    bool ShouldRegister() override;
    void OnReady() override;

    void Attach(Msm8255MdpRegisterRead reg, Completion complete);
    void SyncConfigWritten(uint32_t block, uint32_t old_cfg);
    void SyncRegisterWritten();
    void RequireIdle(uint32_t block) const;
    void Start(uint32_t block, uint32_t link_bytes, uint32_t done_bits);
    bool Busy(uint32_t block) const { return transfers_[block].phase != Phase::kIdle; }

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    static constexpr uint32_t kBlocks = 2u;

    enum class Phase : uint8_t { kIdle, kWaiting, kSending };

    struct Counter {
        bool     anchored    = false;
        uint64_t anchor_tick = 0;
        uint32_t anchor_line = 0;
    };

    struct Transfer {
        Phase                  phase     = Phase::kIdle;
        uint32_t               done_bits = 0;
        uint64_t               link_ns   = 0;
        int64_t                done_ns   = 0;
        GuestCycleClock::Event* start     = nullptr;
        GuestCycleClock::Event* done      = nullptr;
    };

    uint32_t Cfg(uint32_t block) const;
    bool     TearEnabled(uint32_t block) const;
    uint32_t LineAt(uint32_t block, uint64_t tick) const;
    uint64_t StartTick(uint32_t block);
    void     Plan(uint32_t block);
    void     BeginSend(uint32_t block);
    void     Complete(uint32_t block);
    void     OnVsyncInput(bool high);
    void     OnVsyncClock(bool on);
    void     OnPmdhClock(bool on);
    void     OnRateChange();
    void     Rearm();
    void     Reset();

    Msm8255MdpRegisterRead         reg_;
    Completion                     complete_;
    GuestCycleClock*               clock_ = nullptr;
    StoppableOscillatorTicks       vsync_;
    std::array<Counter, kBlocks>   counters_{};
    std::array<Transfer, kBlocks>  transfers_{};
};

}
