#include "msm8255_dem_master.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>
#include "../../core/deferred_jobs.h"
#include "msm8255_acc.h"
#include "msm8255_id.h"
#include "msm8255_modem_peer.h"
#include "msm8255_saw.h"
#include "msm8255_smem.h"

#include <bit>

namespace cerfcore {

namespace {

constexpr uint32_t kItemAppsDem        = 3u;
constexpr uint32_t kItemPowerMasterDem = 6u;

constexpr uint32_t kSlaveRun           = 0x0001u;
constexpr uint32_t kSlavePwrc          = 0x0002u;
constexpr uint32_t kSlavePwrcEarlyExit = 0x0008u;
constexpr uint32_t kSlaveWfpi          = 0x0010u;
constexpr uint32_t kSlaveSleep         = 0x0020u;
constexpr uint32_t kSlaveSleepExit     = 0x0040u;

constexpr uint32_t kMasterRun           = 0x01u;
constexpr uint32_t kMasterRsa           = 0x02u;
constexpr uint32_t kMasterPwrcEarlyExit = 0x04u;
constexpr uint32_t kMasterSleepExit     = 0x08u;
constexpr uint32_t kMasterReady         = 0x10u;
constexpr uint32_t kMasterSleep         = 0x20u;
constexpr uint32_t kMasterAppsMask      = 0x3Fu;

constexpr uint32_t kSmemAppsDemSlaveData = 334u;
constexpr uint32_t kSlaveSleepTimeOff    = 0x00u;
constexpr uint32_t kSlaveIrqMaskOff      = 0x04u;
constexpr uint32_t kSlaveWakeupReasonOff = 0x10u;
constexpr uint32_t kSlavePendingIrqsOff  = 0x14u;
constexpr uint32_t kSlaveRpcProgOff      = 0x18u;
constexpr uint32_t kSlaveRpcProcOff      = 0x1Cu;
constexpr uint32_t kSlavePortNameOff     = 0x20u;
constexpr uint32_t kSlaveDataMinBytes    = 0x34u;

constexpr uint32_t kWakeupReasonRpc   = 0x01u;
constexpr uint32_t kWakeupReasonInt   = 0x02u;
constexpr uint32_t kWakeupReasonTimer = 0x08u;
constexpr int64_t  kSleepClockHz      = 32768;
constexpr int64_t  kNsPerSecond       = 1000000000;

constexpr int8_t kNoVicSource = -1;
constexpr int8_t kWakeBitSource[32] = {
    43, 44, 45, 60, 53, 94, 95, 98,
    99, 20, 67, 69, 71, 68, 70, 72,
    73, 74, 59, kNoVicSource, 41, 40, 56, 55,
    57, 60, 76, 75, 101, 100, 97, 96,
};

bool WakeBitSource(uint32_t bit, uint32_t& source) {
    if (kWakeBitSource[bit] == kNoVicSource) return false;
    source = static_cast<uint32_t>(kWakeBitSource[bit]);
    return true;
}

uint32_t MasterFor(uint32_t slave) {
    if ((slave & kSlavePwrcEarlyExit) != 0u) return kMasterReady | kMasterPwrcEarlyExit;
    if ((slave & kSlavePwrc) != 0u)          return kMasterReady | kMasterRsa;
    if ((slave & kSlaveSleepExit) != 0u)     return kMasterReady | kMasterSleepExit;
    if ((slave & kSlaveSleep) != 0u)         return kMasterReady | kMasterSleep;
    if ((slave & (kSlaveWfpi | kSlaveRun)) != 0u) return kMasterReady | kMasterRun;
    return kMasterReady;
}

}

bool Msm8255DemMaster::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255DemMaster::OnReady() {
    acc_        = &emu_.Get<Msm8255Acc>();
    saw_        = &emu_.Get<Msm8255Saw>();
    clock_      = &emu_.Get<GuestCycleClock>();
    wake_event_ = clock_->Add([this] { WakeOnSleepTimer(); });
    clock_->RegisterRateListener([this] {
        if (wake_armed_) clock_->Arm(wake_event_, clock_->CycleAtNs(wake_due_ns_));
    });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        clock_->Disarm(wake_event_);
        episode_    = Episode::None;
        wake_armed_ = false;
        woken_      = false;
        wake_mask_  = 0u;
        monitored_  = {};
        for (auto& bank : raised_) bank.store(0u, std::memory_order_release);
    });
    clock_->RegisterIdleListener([this] {
        if (episode_ == Episode::PowerCollapse && acc_->AppsPowerDown()) saw_->CollapseCore();
    });
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] {
        clock_->Disarm(wake_event_);
        if (episode_ != Episode::None && !woken_) MonitorEpisodeSources();
        PostRaisedSources();
        if (!wake_armed_) return;
        wake_due_ns_ = clock_->NowNs() + restored_remaining_ns_;
        clock_->Arm(wake_event_, clock_->CycleAtNs(wake_due_ns_));
    });
}

bool Msm8255DemMaster::Answer(uint32_t smsm_state_pa) {
    auto&          mem    = emu_.Get<EmulatedMemory>();
    const uint32_t slave  = mem.ReadWord(smsm_state_pa + 4u * kItemAppsDem);
    const uint32_t master = mem.ReadWord(smsm_state_pa + 4u * kItemPowerMasterDem);
    FollowSlave(slave);
    const uint32_t next = (master & ~kMasterAppsMask) | MasterFor(slave);
    if (next == master) return false;
    mem.WriteWord(smsm_state_pa + 4u * kItemPowerMasterDem, next);
    return true;
}

void Msm8255DemMaster::FollowSlave(uint32_t slave) {
    const Episode episode = (slave & kSlavePwrc) != 0u  ? Episode::PowerCollapse
                            : (slave & kSlaveSleep) != 0u ? Episode::Sleep
                                                          : Episode::None;
    if (episode == episode_) return;
    if (episode_ == Episode::PowerCollapse) saw_->RequestPowerUp(false);
    episode_ = episode;
    clock_->Disarm(wake_event_);
    wake_armed_ = false;
    woken_      = false;
    wake_mask_  = 0u;
    monitored_  = {};
    for (auto& bank : raised_) bank.store(0u, std::memory_order_release);
    if (episode == Episode::None) {
        emu_.Get<Msm8255Vic>().MonitorWakeSources(monitored_);
        return;
    }
    const uint32_t ticks = emu_.Get<EmulatedMemory>().ReadWord(SlaveDataPa() + kSlaveSleepTimeOff);
    if (ticks != 0u) {
        wake_due_ns_ = clock_->NowNs() + static_cast<int64_t>(ticks) * kNsPerSecond / kSleepClockHz;
        wake_armed_  = true;
        clock_->Arm(wake_event_, clock_->CycleAtNs(wake_due_ns_));
    }
    MonitorEpisodeSources();
}

void Msm8255DemMaster::MonitorEpisodeSources() {
    const uint32_t mask = emu_.Get<EmulatedMemory>().ReadWord(SlaveDataPa() + kSlaveIrqMaskOff);
    wake_mask_ = mask;
    monitored_ = {};
    for (uint32_t bits = mask; bits != 0u; bits &= bits - 1u) {
        uint32_t source = 0u;
        if (WakeBitSource(static_cast<uint32_t>(std::countr_zero(bits)), source)) monitored_.Add(source);
    }
    emu_.Get<Msm8255Vic>().MonitorWakeSources(monitored_);
}

void Msm8255DemMaster::WakeSourceRaised(uint32_t source) {
    const Msm8255Vic::SourceLine line = Msm8255Vic::LineOf(source);
    raised_[line.bank].fetch_or(line.mask, std::memory_order_acq_rel);
    PostRaisedSources();
}

void Msm8255DemMaster::PostRaisedSources() {
    bool any = false;
    for (const auto& bank : raised_) any = any || bank.load(std::memory_order_acquire) != 0u;
    if (!any || raise_posted_.exchange(true, std::memory_order_acq_rel)) return;
    emu_.Get<DeferredJobs>().PostToGuestThread([this] { TakeRaisedSources(); });
}

void Msm8255DemMaster::TakeRaisedSources() {
    raise_posted_.store(false, std::memory_order_release);
    Msm8255Vic::SourceSet raised{};
    for (uint32_t b = 0; b < Msm8255Vic::kBankCount; ++b) {
        raised.banks[b] = raised_[b].exchange(0u, std::memory_order_acq_rel) & monitored_.banks[b];
    }
    if (episode_ == Episode::None || woken_) return;
    uint32_t pending = 0u;
    for (uint32_t bits = wake_mask_; bits != 0u; bits &= bits - 1u) {
        const uint32_t bit    = static_cast<uint32_t>(std::countr_zero(bits));
        uint32_t       source = 0u;
        if (WakeBitSource(bit, source) && raised.Has(source)) pending |= 1u << bit;
    }
    if (pending == 0u) return;
    Deliver(kWakeupReasonInt, pending);
}

void Msm8255DemMaster::WakeForRpc(uint32_t prog, uint32_t proc, const PortName& port) {
    if (episode_ == Episode::None || woken_) return;
    auto&          mem  = emu_.Get<EmulatedMemory>();
    const uint32_t data = SlaveDataPa();
    mem.WriteWord(data + kSlaveRpcProgOff, prog);
    mem.WriteWord(data + kSlaveRpcProcOff, proc);
    for (uint32_t i = 0; i < kPortNameBytes; ++i) mem.WriteByte(data + kSlavePortNameOff + i, port[i]);
    Deliver(kWakeupReasonRpc, 0u);
}

void Msm8255DemMaster::Deliver(uint32_t reason, uint32_t pending_irqs) {
    woken_      = true;
    wake_armed_ = false;
    clock_->Disarm(wake_event_);
    monitored_ = {};
    emu_.Get<Msm8255Vic>().MonitorWakeSources(monitored_);
    auto&          mem  = emu_.Get<EmulatedMemory>();
    const uint32_t data = SlaveDataPa();
    mem.WriteWord(data + kSlaveWakeupReasonOff, reason);
    mem.WriteWord(data + kSlavePendingIrqsOff, pending_irqs);
    if (episode_ == Episode::PowerCollapse) saw_->RequestPowerUp(true);
    else                                    emu_.Get<Msm8255ModemPeer>().RaiseM2a6();
}

uint32_t Msm8255DemMaster::SlaveDataPa() {
    uint32_t pa    = 0u;
    uint32_t bytes = 0u;
    if (!emu_.Get<Msm8255Smem>().ItemPaAndSize(kSmemAppsDemSlaveData, pa, bytes) ||
        bytes < kSlaveDataMinBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 dem master: the apps entered a DEM low-power episode with smem item %u "
            "holding %u bytes", kSmemAppsDemSlaveData, bytes);
    }
    return pa;
}

void Msm8255DemMaster::WakeOnSleepTimer() {
    Deliver(kWakeupReasonTimer, 0u);
}

void Msm8255DemMaster::SaveState(StateWriter& w) {
    w.Write<uint32_t>("dem_episode", static_cast<uint32_t>(episode_));
    w.Write<uint32_t>("dem_wake_armed", wake_armed_ ? 1u : 0u);
    w.Write<int64_t>("dem_wake_remaining_ns", wake_armed_ ? wake_due_ns_ - clock_->NowNs() : 0);
    w.Write<uint32_t>("dem_woken", woken_ ? 1u : 0u);
    for (const auto& bank : raised_) {
        w.Write<uint32_t>("dem_raised_sources", bank.load(std::memory_order_acquire));
    }
}

void Msm8255DemMaster::RestoreState(StateReader& r) {
    uint32_t episode    = 0u;
    uint32_t wake_armed = 0u;
    uint32_t woken      = 0u;
    r.Read("dem_episode", episode);
    r.Read("dem_wake_armed", wake_armed);
    r.Read("dem_wake_remaining_ns", restored_remaining_ns_);
    r.Read("dem_woken", woken);
    for (auto& bank : raised_) {
        uint32_t sources = 0u;
        r.Read("dem_raised_sources", sources);
        bank.store(sources, std::memory_order_release);
    }
    episode_    = static_cast<Episode>(episode);
    wake_armed_ = wake_armed != 0u;
    woken_      = woken != 0u;
    raise_posted_.store(false, std::memory_order_release);
}

REGISTER_SERVICE(Msm8255DemMaster);

}
