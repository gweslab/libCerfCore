#pragma once

#include "vr41xx_buffered_piu.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "vr41xx_cmu.h"
#include "vr41xx_icu.h"
#include "vr41xx_piu_converter.h"
#include "vr41xx_piu_host_pen.h"
#include "vr41xx_piu_pen_contact.h"
#include "vr41xx_piu_sample_schedule.h"
#include "vr41xx_piu_scan_timing.h"
#include "vr41xx_piu_state_table.h"
#include "vr41xx_rtcx_ticks.h"

#include <cstdint>
#include <mutex>
#include <optional>

#include "vr41xx_piu_regs.h"

namespace cerfcore {

namespace cerf_vr41xx_piu_detail {


template <const std::string_view& Soc, const Vr41xxPiuModel& M>
class Vr41xxPiuBase : public Vr41xxBufferedPiu {
public:
    using Vr41xxBufferedPiu::Vr41xxBufferedPiu;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == Soc;
    }

    /* Every PIU register's RTCRST column equals its other-resets column (VR4121 UM
       20.3.1-20.3.10, VR4102 UM 19.3.1-19.3.10). */
    void OnReady() override {
        clock_ = &emu_.Get<GuestCycleClock>();
        scan_.Attach(clock_, &rtcx_);
        event_ = clock_->Add([this] {
            std::lock_guard<std::mutex> lk(mtx_);
            SampleDueLocked();
        });
        rtcx_.Attach([this] {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!rtcx_.Running()) {
                AdvanceLocked();
                if (state_ != kStDisable && state_ != kStStandby && state_ != kStWaitPenTouch) {
                    emu_.Get<Fatal>().Die("VR41xx PIU: SUSPEND entered in PADSTATE %u; the "
                                          "sequencer outside Disable, Standby and WaitPenTouch "
                                          "in Suspend is not modeled", state_);
                }
            }
            ApplyHostPenLocked();
        });
        clock_->RegisterRateListener([this] {
            std::lock_guard<std::mutex> lk(mtx_);
            rtcx_.Rescale();
            scan_.Retime(clock_->Cycles());
            ArmLocked();
        });
        host_pen_.Attach([this] {
            std::lock_guard<std::mutex> lk(mtx_);
            ApplyHostPenLocked();
        });
        cmu_ = &emu_.Get<Vr41xxCmu>();
        cmu_->RegisterClockListener(kCmuMskPiu, [this](bool supplied_after) {
            if (supplied_after) return;
            std::lock_guard<std::mutex> lk(mtx_);
            AdvanceLocked();
            ArmLocked();
            if (scan_.Kind() != kScanIdle)
                emu_.Get<Fatal>().Die("VR41xx PIU: CMUCLKMSK masks the PIU TClock during scan %u; "
                                      "the unit without TClock is not modeled", scan_.Kind());
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            std::lock_guard<std::mutex> lk(mtx_);
            ApplyResetLocked();
            ArmLocked();
        });
    }

    uint32_t MmioBase() const override { return M.base; }
    uint32_t MmioSize() const override { return M.size; }
    uint32_t Piu2Base() const override { return M.piu2_base; }
    uint32_t Piu2Size() const override { return M.piu2_size; }

    uint16_t ReadHalf(uint32_t addr) override {
        std::lock_guard<std::mutex> lk(mtx_);
        AdvanceLocked();
        switch (addr - M.base) {
            case kOffCnt:
                return CntReadValue(M.has_penstp && contact_.Previous(), contact_.Stc(), state_, cnt_cfg_);
            case kOffInt:  return intreg_;
            case kOffSivl: return sivl_;
            case kOffStbl: return stbl_;
            case kOffCmd:  return cmd_;
            case kOffAscn: return ascn_;
            case kOffAmsk: return amsk_;
            default: HaltUnsupportedAccess("VR41xx PIU ReadHalf", addr, 0);
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        std::lock_guard<std::mutex> lk(mtx_);
        AdvanceLocked();
        switch (addr - M.base) {
            case kOffCnt: ApplyCntWriteLocked(value); return;
            case kOffInt: AckIntLocked(value); AdvanceLocked(); return;
            /* PIUSIVLREG D10:0 SCANINTVAL, PIUSTBLREG D5:0 STABLE, PIUCMDREG D12:0
               (VR4121 UM 20.3.3/20.3.4/20.3.5, VR4102 UM 19.3.3/19.3.4/19.3.5). */
            case kOffSivl: sivl_ = value & 0x07FFu; ArmLocked(); return;
            case kOffStbl: stbl_ = value & 0x003Fu; return;
            case kOffCmd:  cmd_  = value & 0x1FFFu; return;
            /* PIUASCNREG D1:0 (VR4121 UM 20.3.6, VR4102 UM 19.3.6); PIUAMSKREG D7:0 (VR4121 UM
               20.3.7, VR4102 UM 19.3.7). */
            case kOffAscn:
                ascn_ = value & 0x0003u;
                if (StartAdPortScanFromWaitLocked(clock_->Cycles())) ArmLocked();
                return;
            case kOffAmsk: amsk_ = value & 0x00FFu; return;
            default: HaltUnsupportedAccess("VR41xx PIU WriteHalf", addr, value);
        }
    }

    uint32_t ReadWord  (uint32_t addr) override { return ReadWordAsHalves(addr); }
    uint64_t ReadDword (uint32_t addr) override { return ReadDwordAsHalves(addr); }
    void     WriteWord (uint32_t addr, uint32_t v) override { WriteWordAsHalves(addr, v); }
    void     WriteDword(uint32_t addr, uint64_t v) override { WriteDwordAsHalves(addr, v); }

    void SetPen(bool down, uint16_t pos_x, uint16_t pos_y) override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            host_pen_.Pen(down, pos_x, pos_y);
        }
        host_pen_.Wake();
    }

    void SyntheticTap(uint16_t pos_x, uint16_t pos_y) override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            host_pen_.Tap(pos_x, pos_y);
        }
        host_pen_.Wake();
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mtx_);
        w.Write("state", state_); w.Write("cnt_cfg", cnt_cfg_); w.Write("intreg", intreg_);
        w.Write("sivl", sivl_); w.Write("stbl", stbl_); w.Write("cmd", cmd_);
        contact_.Save(w);
        w.Write("pos_x", pos_x_); w.Write("pos_y", pos_y_);
        w.Write("ascn", ascn_);
        w.Write("amsk", amsk_);
        w.Write("adps_return", adps_return_);
        w.Write("next_sample_in", schedule_.TicksToNext());
        scan_.Save(w, clock_->Cycles());
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mtx_);
        r.Read("state", state_); r.Read("cnt_cfg", cnt_cfg_); r.Read("intreg", intreg_);
        r.Read("sivl", sivl_); r.Read("stbl", stbl_); r.Read("cmd", cmd_);
        contact_.Restore(r, (intreg_ & kPenChgIntr) != 0);
        r.Read("pos_x", pos_x_); r.Read("pos_y", pos_y_);
        r.Read("ascn", ascn_);
        r.Read("amsk", amsk_);
        r.Read("adps_return", adps_return_);
        uint64_t next_in = 0;
        r.Read("next_sample_in", next_in);
        scan_.Restore(r);
        if (SamplingLocked()) {
            intreg_ |= kPenChgIntr;
            state_ = (cnt_cfg_ & kAtStop) ? kStWaitPenTouch : kStIntervalNext;
        }
        AbandonScanOutsideStateLocked();
        host_pen_.Clear();
        rtcx_.Rebase();
        schedule_.SetTicksToNext(next_in);
    }

    void PostRestore() override {
        std::lock_guard<std::mutex> lk(mtx_);
        DriveIcuLocked();
        ArmLocked();
    }

private:

    /* PADATSTART / PADATSTOP (VR4121 UM 20.3.1, VR4102 UM 19.3.1). */
    void PenEdgeLocked(bool down) {
        contact_.Edge(down, (intreg_ & kPenChgIntr) != 0);
        DetectPenLocked();
        if (down) {
            StartTouchScanLocked();
        } else if ((cnt_cfg_ & kAtStop) && DetectingStateLocked() == kStIntervalNext) {
            /* "Release & AutoStop = 1" leaves IntervalNextScan, which PenDataScan enters on
               "auto" (VR4121 UM Figure 20-4). */
            state_ = kStWaitPenTouch;
        }
        DriveIcuLocked();
    }

    uint16_t DetectingStateLocked() const { return DetectingState(state_, scan_.Kind()); }

    void DetectPenLocked() {
        if (PenChangeDetected(DetectingStateLocked(), contact_.Down())) intreg_ |= kPenChgIntr;
    }

    /* "when PENCHGINTR is cleared to 0, PENSTC indicates the touch panel contact state"
       (VR4121 UM 20.3.1). The VALID bit of a page buffer "is automatically rendered
       invalid when the page buffer interrupt source (PIUPAGE0INTR or PIUPAGE1INTR) is
       cleared" (VR4121 UM 20.3.9, VR4102 UM 19.3.9). */
    void AckIntLocked(uint16_t value) {
        const uint16_t clr = value & kIntCauses;
        intreg_ &= ~clr;
        if (clr & kPage0Intr) Converter().InvalidatePage(0);
        if (clr & kPage1Intr) Converter().InvalidatePage(1);
        if (clr & kPadAdpIntr) Converter().InvalidateAdBuffer();
        if (clr & kPenChgIntr) contact_.PenChgCleared();
        DriveIcuLocked();
    }

    void ApplyCntWriteLocked(uint16_t value) {
        /* PADRST 0->1 is "-" from Disable and "Disable" from every other state; PIUPWR 0->1
           from Disable is "Standby" (VR4121 UM Table 20-2, VR4102 UM Table 19-2). nk.exe
           sub_9F0B61DC stores PADRST|PIUPWR in one halfword (MEMORY[0xAB000122] = 3), then
           spins for PADSTATE == Standby. */
        if (value & kPadRst) ApplyResetLocked();

        const uint16_t old = cnt_cfg_;
        if (CntStrobeProhibited(state_, old, value & kCntStored)) {
            HaltUnsupportedAccess("VR41xx PIU PIUCNTREG strobe prohibited in this PADSTATE", M.base + kOffCnt, value);
        }
        cnt_cfg_ = value & kCntStored;
        const uint16_t next = StateAfterCntWrite(state_, old, cnt_cfg_, ascn_);
        if (IntervalScanStopped(state_, old, cnt_cfg_)) cnt_cfg_ = static_cast<uint16_t>(cnt_cfg_ & ~kScanStop);
        const bool forced_start = state_ == kStStandby && next == kStPenDataScan;
        const bool entered_wait = state_ != kStWaitPenTouch && next == kStWaitPenTouch;
        state_ = next;
        if (forced_start) BeginDataScanLocked();
        if (entered_wait) {
            DetectPenLocked();
            DriveIcuLocked();
        }

        AbandonScanOutsideStateLocked();
        if (state_ == kStAdPortScan && scan_.Kind() == kScanIdle) {
            adps_return_ = kStStandby;
            BeginScanLocked(kScanAdPort, rtcx_.Now(), clock_->Cycles());
        }
        StartTouchScanLocked();
        if (state_ == kStCmdScan) {
            /* A command scan fetches "one port only" per PIUSEQEN kick and CmdScan has no
               self-loop (VR4121 UM 20.2 (4), Figure 20-4; VR4102 UM 19.2, Figure 19-4);
               touch.dll sub_15A0E24 re-arms "PIUCNTREG |= PIUSEQEN" after every sample. */
            if (scan_.Kind() != kScanIdle) CompleteScanLocked();
            BeginScanLocked(kScanCmd, rtcx_.Now(), clock_->Cycles());
        }
        StartAdPortScanFromWaitLocked(clock_->Cycles());
        ArmLocked();
    }

    /* PADATSTART "1: Auto start during touch state" (VR4102 UM 19.3.1, printed p.387). */
    void StartTouchScanLocked() {
        if (contact_.Down() && (cnt_cfg_ & kSeqEn) && (cnt_cfg_ & kAtStart) && DetectingStateLocked() == kStWaitPenTouch) {
            state_ = kStPenDataScan;
        }
        if (contact_.Down()) BeginDataScanLocked();
    }

    void BeginDataScanLocked() {
        if (state_ != kStPenDataScan || scan_.Kind() != kScanIdle) return;
        if (!rtcx_.Running()) {
            schedule_.HoldAtNow();
            return;
        }
        const uint64_t now_cycle = clock_->Cycles();
        BeginScanLocked(kScanData, rtcx_.Now(), now_cycle);
        schedule_.StartAtNow(IntervalTicksLocked());
        SkipLostStartsLocked(now_cycle + scan_.DataScanCycles(stbl_));
    }

    void SkipLostStartsLocked(uint64_t busy_end) {
        if (!schedule_.SkipLost(busy_end, IntervalTicksLocked())) return;
        intreg_ |= kPadDlostIntr;
        DriveIcuLocked();
    }

    /* VR4102 UM Figure 19-4 (printed p.384): WaitPenTouch and Interval go to ADPortScan on
       "ADPSStart=1"; 19.2: "the state changes to the state in which the ADPSSTART bit was
       active". */
    bool StartAdPortScanFromWaitLocked(uint64_t at_cycle) {
        if ((ascn_ & kAdpsStart) == 0u || scan_.Kind() != kScanIdle) return false;
        if (state_ != kStWaitPenTouch && state_ != kStIntervalNext) return false;
        adps_return_ = state_;
        state_       = kStAdPortScan;
        BeginScanLocked(kScanAdPort, rtcx_.Now(), at_cycle);
        return true;
    }

    void BeginScanLocked(uint16_t kind, uint64_t start_tick, uint64_t start_cycle) {
        cmu_->RequireClock(kCmuMskPiu, "PIU scan start");
        scan_.Begin(kind, stbl_, start_tick, start_cycle);
    }

    void CompleteScanLocked() {
        const uint64_t done = scan_.ReadyAt();
        const uint16_t kind = scan_.Take();
        if (kind == kScanData) {
            const int page = Converter().ConvertCoordinates(pos_x_, pos_y_, contact_.Down());
            /* PIUINTREG OVP "1: Valid data older than page 1 buffer data is retained / 0: Valid
               data older than page 0 buffer data is retained" (VR4121 UM 20.3.2, VR4102 UM
               19.3.2). */
            intreg_ |= (page == 0) ? kPage0Intr : kPage1Intr;
            intreg_ = (intreg_ & ~kOvp) | ((page == 1) ? static_cast<uint32_t>(kOvp) : 0u);
            state_ = kStIntervalNext;
            if (!StopAfterScanLocked()) {
                DetectPenLocked();
                if (!contact_.Down() && (cnt_cfg_ & kAtStop)) state_ = kStWaitPenTouch;
            }
            if (synthetic_hold_ != 0 && --synthetic_hold_ == 0) PenEdgeLocked(false);
            const uint64_t adps_at = done + scan_.DataScanCycles(stbl_) + scan_.SettleCycles(stbl_);
            if ((ascn_ & kAdpsStart) && ScanKeptInState(kScanAdPort, state_)) {
                BeginScanLocked(kScanAdPort, rtcx_.Now(), adps_at);
            }
        } else if (kind == kScanCmd) {
            if (Converter().ConvertCommand(cmd_, pos_x_, pos_y_, contact_.Down())) intreg_ |= kPadCmdIntr;
            StopAfterScanLocked();
        } else {
            if (Converter().ScanAdPorts(ascn_, amsk_)) intreg_ |= kPadAdpIntr;
            ascn_ &= ~kAdpsStart;
            if (state_ == kStAdPortScan) state_ = adps_return_;
            if (StopAfterScanLocked()) {
                DriveIcuLocked();
                return;
            }
            if (state_ == kStIntervalNext) SkipLostStartsLocked(done + scan_.AdPortScanCycles());
            DetectPenLocked();
            StartTouchScanLocked();
        }
        DriveIcuLocked();
    }

    bool StopAfterScanLocked() {
        if (!(cnt_cfg_ & kScanStop)) return false;
        cnt_cfg_ = static_cast<uint16_t>(cnt_cfg_ & ~kScanStop);
        state_   = kStStandby;
        return true;
    }

    void AbandonScanOutsideStateLocked() {
        if (!ScanKeptInState(scan_.Kind(), state_)) scan_.Drop();
    }

    /* The ICU's PIUINTREG (0x0B000082) carries the PIU's interrupt causes and raises
       SYSINT1REG PIUINTR when unmasked (VR4121 UM 15.1, VR4102 UM 14.1). */
    void DriveIcuLocked() {
        emu_.Get<Vr41xxIcu>().SetPiuSource(intreg_ & kIntCauses);
    }

    Vr41xxPiuConverter& Converter() { return emu_.Get<Vr41xxPiuConverter>(); }

    /* "Interval = SCANINTVAL(10:0) x 30 us" (VR4121 UM 20.3.3, VR4102 UM 19.3.3). */
    uint64_t IntervalTicksLocked() const { return sivl_ & 0x07FFu; }

    bool SamplingLocked() const { return state_ == kStPenDataScan || state_ == kStIntervalNext; }

    bool IntervalRunningLocked() const {
        return SamplingLocked() || (state_ == kStAdPortScan && adps_return_ == kStIntervalNext);
    }

    bool IntervalScanStartedLocked() {
        return IntervalRunningLocked() && schedule_.Due();
    }

    void AdvanceLocked() {
        for (;;) {
            const bool start = IntervalScanStartedLocked();
            if (scan_.Kind() != kScanIdle && clock_->Cycles() >= scan_.ReadyAt() &&
                (!start || scan_.ReadyAt() <= schedule_.NextCycle())) {
                CompleteScanLocked();
                continue;
            }
            if (!start) return;
            if (scan_.Kind() == kScanAdPort) {
                SkipLostStartsLocked(scan_.ReadyAt() + scan_.AdPortScanCycles());
                continue;
            }
            state_ = kStPenDataScan;
            const uint64_t start_tick  = schedule_.Take(IntervalTicksLocked());
            const uint64_t start_cycle = rtcx_.CycleOf(start_tick);
            BeginScanLocked(kScanData, start_tick, start_cycle);
            SkipLostStartsLocked(start_cycle + scan_.DataScanCycles(stbl_));
        }
    }

    void SampleDueLocked() {
        AdvanceLocked();
        ArmLocked();
    }

    void ArmLocked() {
        if (!rtcx_.Running() || (scan_.Kind() == kScanIdle && !IntervalRunningLocked())) {
            clock_->Disarm(event_);
            return;
        }
        uint64_t at = scan_.Kind() != kScanIdle ? scan_.ReadyAt() : UINT64_MAX;
        const uint64_t start = schedule_.NextCycle();
        if (IntervalRunningLocked() && start < at) at = start;
        if (at == UINT64_MAX) clock_->Disarm(event_); else clock_->Arm(event_, at);
    }

    void ApplyHostPenLocked() {
        if constexpr (!M.pen_detect_in_suspend) {
            if (!rtcx_.Running()) {
                ArmLocked();
                return;
            }
        }
        AdvanceLocked();
        if (const std::optional<Vr41xxPiuPenPoint> p = host_pen_.TakePen()) {
            pos_x_          = p->x;
            pos_y_          = p->y;
            synthetic_hold_ = 0;
            if (p->down != contact_.Down()) PenEdgeLocked(p->down);
        }
        if (const std::optional<Vr41xxPiuPenPoint> t = host_pen_.TakeTap(); t && !contact_.Down()) {
            pos_x_          = t->x;
            pos_y_          = t->y;
            synthetic_hold_ = kSyntheticHoldScans;
            PenEdgeLocked(true);
        }
        StartAdPortScanFromWaitLocked(clock_->Cycles());
        ArmLocked();
    }

    void ApplyResetLocked() {
        state_       = kStDisable;
        adps_return_ = kStStandby;
        cnt_cfg_     = 0;
        intreg_    = 0;
        sivl_      = M.sivl_power_on;
        stbl_      = kStblPowerOn;
        cmd_       = kCmdPowerOn;
        contact_.Reset();
        Converter().Reset();
        ascn_      = 0;
        amsk_      = 0;
        scan_.Drop();
        DriveIcuLocked();
    }

    mutable std::mutex mtx_;

    uint16_t state_    = kStDisable;
    uint16_t adps_return_ = kStStandby;
    uint16_t cnt_cfg_  = 0;
    uint16_t intreg_   = 0;
    uint16_t sivl_     = M.sivl_power_on;
    uint16_t stbl_     = kStblPowerOn;
    uint16_t cmd_      = kCmdPowerOn;

    Vr41xxPiuPenContact contact_{M.penstc_latched_by_penchg};
    uint16_t pos_x_    = 0;
    uint16_t pos_y_    = 0;
    uint16_t synthetic_hold_ = 0;

    uint16_t ascn_ = 0;
    uint16_t amsk_ = 0;

    static constexpr uint16_t kScanIdle   = Vr41xxPiuScanTiming::kIdle;
    static constexpr uint16_t kScanData   = Vr41xxPiuScanTiming::kData;
    static constexpr uint16_t kScanCmd    = Vr41xxPiuScanTiming::kCmd;
    static constexpr uint16_t kScanAdPort = Vr41xxPiuScanTiming::kAdPort;
    Vr41xxPiuScanTiming scan_;

    GuestCycleClock*        clock_       = nullptr;
    GuestCycleClock::Event* event_       = nullptr;
    Vr41xxCmu*              cmu_         = nullptr;
    Vr41xxRtcxTicks         rtcx_{emu_, Vr41xxRtcxDomain::Peripheral};
    Vr41xxPiuSampleSchedule schedule_{rtcx_};
    Vr41xxPiuHostPen        host_pen_{emu_};
};

}  /* namespace cerf_vr41xx_piu_detail */

}
