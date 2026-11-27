#include "msm8255_mdp_frame_timing.h"

#include "msm8255_clock_enables.h"
#include "msm8255_clock_rates.h"
#include "msm8255_gpio_bus.h"
#include "msm8255_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kRegSyncCfg[]    = {0x00100u, 0x00104u};
constexpr uint32_t kRegSyncThresh[] = {0x00200u, 0x00204u};
constexpr uint32_t kRegStartPos[]   = {0x00210u, 0x00214u};
constexpr uint32_t kRegVsyncInit[]  = {0x00128u, 0x0012Cu};
constexpr uint32_t kRegTearEnable   = 0x0020Cu;
constexpr uint32_t kRegVsyncSel     = 0x00124u;

constexpr uint32_t kHeightShift   = 21u;
constexpr uint32_t kExtEnable     = 1u << 20;
constexpr uint32_t kIntEnable     = 1u << 19;
constexpr uint32_t kCountMask     = (1u << 19) - 1u;
constexpr uint32_t kThreshStart   = 0xFFFFu;
constexpr uint32_t kVsyncSelPin   = 0u;

constexpr uint32_t kVsyncPin      = 30u;
constexpr uint32_t kVsyncFunction = 1u;

constexpr uint64_t kLinkNsPerByteKhz = 4000000u;

constexpr const char* kBlockName[] = {"the primary dma", "the secondary dma"};

}

REGISTER_SERVICE(Msm8255MdpFrameTiming);

bool Msm8255MdpFrameTiming::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255MdpFrameTiming::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    auto& enables = emu_.Get<Msm8255ClockEnables>();
    vsync_.Attach(kMsm8255MdpVsyncHz, 1u);
    vsync_.SetCounting(enables.Enabled(kMsm8255MdpVsyncClock));
    for (uint32_t block = 0; block < kBlocks; ++block) {
        transfers_[block].start = clock_->Add([this, block] { BeginSend(block); });
        transfers_[block].done  = clock_->Add([this, block] { Complete(block); });
    }
    clock_->RegisterRateListener([this] { OnRateChange(); });
    enables.AddListener(kMsm8255MdpVsyncClock, [this](bool on) { OnVsyncClock(on); });
    enables.AddListener(kMsm8255PmdhClock, [this](bool on) { OnPmdhClock(on); });
    emu_.Get<Msm8255GpioBus>().AddInputListener(kVsyncPin, [this](bool high) { OnVsyncInput(high); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { Reset(); });
}

void Msm8255MdpFrameTiming::Attach(Msm8255MdpRegisterRead reg, Completion complete) {
    reg_      = std::move(reg);
    complete_ = std::move(complete);
}

uint32_t Msm8255MdpFrameTiming::Cfg(uint32_t block) const { return reg_(kRegSyncCfg[block]); }

bool Msm8255MdpFrameTiming::TearEnabled(uint32_t block) const {
    return (reg_(kRegTearEnable) & (1u << block)) != 0u;
}

uint32_t Msm8255MdpFrameTiming::LineAt(uint32_t block, uint64_t tick) const {
    const Counter& c     = counters_[block];
    const uint32_t cfg   = Cfg(block);
    const uint64_t lines = uint64_t(cfg >> kHeightShift) + 1u;
    return static_cast<uint32_t>((c.anchor_line + (tick - c.anchor_tick) / (cfg & kCountMask)) % lines);
}

void Msm8255MdpFrameTiming::SyncConfigWritten(uint32_t block, uint32_t old_cfg) {
    Counter&       c     = counters_[block];
    const uint32_t cfg   = Cfg(block);
    const uint64_t now   = vsync_.Now();
    const uint32_t lines = (cfg >> kHeightShift) + 1u;
    if ((cfg & kIntEnable) == 0u) {
        c.anchored = false;
    } else if ((old_cfg & kIntEnable) == 0u || !c.anchored || (old_cfg & kCountMask) == 0u) {
        c = Counter{true, now, 0u};
    } else {
        const uint64_t old_lines = uint64_t(old_cfg >> kHeightShift) + 1u;
        const uint64_t line =
            (c.anchor_line + (now - c.anchor_tick) / (old_cfg & kCountMask)) % old_lines;
        c = Counter{true, now, static_cast<uint32_t>(line % lines)};
    }
    if (transfers_[block].phase == Phase::kWaiting) Plan(block);
}

void Msm8255MdpFrameTiming::RequireIdle(uint32_t block) const {
    if (transfers_[block].phase == Phase::kIdle) return;
    emu_.Get<Fatal>().Die("msm8255 mdp frame timing: %s kicked while its frame transfer is pending",
                          kBlockName[block]);
}

void Msm8255MdpFrameTiming::SyncRegisterWritten() {
    for (uint32_t block = 0; block < kBlocks; ++block) {
        Transfer& t = transfers_[block];
        if (t.phase != Phase::kWaiting) continue;
        if (TearEnabled(block)) {
            Plan(block);
            continue;
        }
        clock_->Disarm(t.start);
        BeginSend(block);
    }
}

void Msm8255MdpFrameTiming::Start(uint32_t block, uint32_t link_bytes, uint32_t done_bits) {
    Transfer& t       = transfers_[block];
    auto&     enables = emu_.Get<Msm8255ClockEnables>();
    enables.RequireRunning(kMsm8255PmdhClock, "mdp frame timing", "sends a frame over the mddi link");
    const uint64_t khz = emu_.Get<Msm8255ClockRates>().ReportClockFreqKhz(kMsm8255PmdhClock);
    t.link_ns   = (uint64_t(link_bytes) * kLinkNsPerByteKhz + khz - 1u) / khz;
    t.done_bits = done_bits;
    if (!TearEnabled(block)) {
        BeginSend(block);
        return;
    }
    enables.RequireRunning(kMsm8255MdpVsyncClock, "mdp frame timing",
                           "holds a frame for its tear check");
    t.phase = Phase::kWaiting;
    Plan(block);
}

uint64_t Msm8255MdpFrameTiming::StartTick(uint32_t block) {
    auto&          fatal = emu_.Get<Fatal>();
    const uint32_t cfg   = Cfg(block);
    const Counter& c     = counters_[block];
    if ((cfg & kIntEnable) == 0u || !c.anchored || (cfg & kCountMask) == 0u) {
        fatal.Die("msm8255 mdp frame timing: %s waits for its tear check with sync config %08X",
                  kBlockName[block], cfg);
    }
    if ((cfg & kExtEnable) != 0u && reg_(kRegVsyncSel) != kVsyncSelPin) {
        fatal.Die("msm8255 mdp frame timing: %s waits for its tear check under vsync select %08X",
                  kBlockName[block], reg_(kRegVsyncSel));
    }
    const uint32_t lines  = (cfg >> kHeightShift) + 1u;
    const uint32_t start  = reg_(kRegStartPos[block]);
    const uint32_t window = reg_(kRegSyncThresh[block]) & kThreshStart;
    if (start >= lines) {
        fatal.Die("msm8255 mdp frame timing: %s start position %u is past the %u lines of its "
                  "vsync counter", kBlockName[block], start, lines);
    }
    const uint64_t now     = vsync_.Now();
    const uint64_t count   = cfg & kCountMask;
    const uint64_t elapsed = (now - c.anchor_tick) / count;
    const uint32_t line    = LineAt(block, now);
    if ((line + lines - start) % lines <= window) return now;
    const uint64_t ahead = (uint64_t(start) + lines - line) % lines;
    return c.anchor_tick + (elapsed + ahead) * count;
}

void Msm8255MdpFrameTiming::Plan(uint32_t block) {
    Transfer& t = transfers_[block];
    clock_->Disarm(t.start);
    if (!emu_.Get<Msm8255ClockEnables>().Enabled(kMsm8255MdpVsyncClock)) return;
    vsync_.ArmAt(t.start, StartTick(block));
}

void Msm8255MdpFrameTiming::BeginSend(uint32_t block) {
    Transfer& t = transfers_[block];
    emu_.Get<Msm8255ClockEnables>().RequireRunning(kMsm8255PmdhClock, "mdp frame timing",
                                                   "starts a frame over the mddi link");
    t.phase   = Phase::kSending;
    t.done_ns = clock_->NowNs() + static_cast<int64_t>(t.link_ns);
    clock_->Arm(t.done, clock_->CycleAtNs(t.done_ns));
}

void Msm8255MdpFrameTiming::Complete(uint32_t block) {
    Transfer& t = transfers_[block];
    t.phase     = Phase::kIdle;
    complete_(t.done_bits);
}

void Msm8255MdpFrameTiming::OnVsyncInput(bool high) {
    if (!high) return;
    const uint32_t cfg = Cfg(kMsm8255MdpSyncPrimary);
    if ((cfg & kIntEnable) == 0u || (cfg & kExtEnable) == 0u) return;
    if (!emu_.Get<Msm8255ClockEnables>().Enabled(kMsm8255MdpVsyncClock)) return;
    if (emu_.Get<Msm8255GpioBus>().PinFunction(kVsyncPin) != kVsyncFunction) return;
    if (reg_(kRegVsyncSel) != kVsyncSelPin) {
        emu_.Get<Fatal>().Die("msm8255 mdp frame timing: a vsync edge under vsync select %08X",
                              reg_(kRegVsyncSel));
    }
    const uint32_t lines = (cfg >> kHeightShift) + 1u;
    counters_[kMsm8255MdpSyncPrimary] =
        Counter{true, vsync_.Now(), reg_(kRegVsyncInit[kMsm8255MdpSyncPrimary]) % lines};
    if (transfers_[kMsm8255MdpSyncPrimary].phase == Phase::kWaiting) Plan(kMsm8255MdpSyncPrimary);
}

void Msm8255MdpFrameTiming::OnVsyncClock(bool on) {
    vsync_.SetCounting(on);
    for (uint32_t block = 0; block < kBlocks; ++block) {
        if (transfers_[block].phase == Phase::kWaiting) Plan(block);
    }
}

void Msm8255MdpFrameTiming::OnPmdhClock(bool on) {
    if (on) return;
    for (uint32_t block = 0; block < kBlocks; ++block) {
        if (transfers_[block].phase != Phase::kSending) continue;
        emu_.Get<Fatal>().Die("msm8255 mdp frame timing: clock %u stopped while %s sends a frame",
                              kMsm8255PmdhClock, kBlockName[block]);
    }
}

void Msm8255MdpFrameTiming::OnRateChange() {
    vsync_.Rescale();
    Rearm();
}

void Msm8255MdpFrameTiming::Reset() {
    for (Transfer& t : transfers_) {
        clock_->Disarm(t.start);
        clock_->Disarm(t.done);
        t.phase = Phase::kIdle;
    }
    counters_.fill(Counter{});
    vsync_.SetCounting(false);
}

void Msm8255MdpFrameTiming::SaveState(StateWriter& w) {
    vsync_.Save(w, "mdp_vsync_");
    for (uint32_t block = 0; block < kBlocks; ++block) {
        const Counter&  c = counters_[block];
        const Transfer& t = transfers_[block];
        w.Write<uint32_t>("vsync_anchored", c.anchored ? 1u : 0u);
        w.Write<uint64_t>("vsync_anchor_tick", c.anchor_tick);
        w.Write<uint32_t>("vsync_anchor_line", c.anchor_line);
        w.Write<uint32_t>("transfer_phase", static_cast<uint32_t>(t.phase));
        w.Write<uint32_t>("transfer_done_bits", t.done_bits);
        w.Write<uint64_t>("transfer_link_ns", t.link_ns);
        w.Write<int64_t>("transfer_done_ns", t.done_ns);
    }
}

void Msm8255MdpFrameTiming::RestoreState(StateReader& r) {
    vsync_.Restore(r, "mdp_vsync_");
    for (uint32_t block = 0; block < kBlocks; ++block) {
        Counter&  c        = counters_[block];
        Transfer& t        = transfers_[block];
        uint32_t  anchored = 0u;
        uint32_t  phase    = 0u;
        r.Read("vsync_anchored", anchored);
        r.Read("vsync_anchor_tick", c.anchor_tick);
        r.Read("vsync_anchor_line", c.anchor_line);
        r.Read("transfer_phase", phase);
        r.Read("transfer_done_bits", t.done_bits);
        r.Read("transfer_link_ns", t.link_ns);
        r.Read("transfer_done_ns", t.done_ns);
        c.anchored = anchored != 0u;
        t.phase    = static_cast<Phase>(phase);
    }
}

void Msm8255MdpFrameTiming::PostRestore() { Rearm(); }

void Msm8255MdpFrameTiming::Rearm() {
    for (uint32_t block = 0; block < kBlocks; ++block) {
        Transfer& t = transfers_[block];
        clock_->Disarm(t.start);
        clock_->Disarm(t.done);
        if (t.phase == Phase::kWaiting) Plan(block);
        if (t.phase == Phase::kSending) clock_->Arm(t.done, clock_->CycleAtNs(t.done_ns));
    }
}

}
