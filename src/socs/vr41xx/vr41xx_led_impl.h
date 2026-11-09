#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "../../core/steady_time.h"
#include <cerfcore/host/host_widget.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "vr41xx_icu.h"
#include "vr41xx_led_second_element.h"
#include "vr41xx_rtcx_ticks.h"

#include <algorithm>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace cerfcore {

namespace cerf_vr41xx_led_detail {

/* VR4111 UM Table 24-1 p493 and VR4102 UM 23.2.1-23.2.5 p454-p458 give 0x0B000240/0242/0248/
   024A/024C; VR4131 UM 19.2.1-19.2.5 p359-p363 give 0x0F000180/0182/0188/018A/018C. */
constexpr uint32_t kOffHts   = 0x00u;
constexpr uint32_t kOffLts   = 0x02u;
constexpr uint32_t kOffHltcl = 0x04u;
constexpr uint32_t kOffHltch = 0x06u;
constexpr uint32_t kOffCnt   = 0x08u;
constexpr uint32_t kOffAstc  = 0x0Au;
constexpr uint32_t kOffInt   = 0x0Cu;

constexpr uint16_t kHltcUnstated = 0x0000u;

/* HTS[4..0] is "Values compared to bits 15 to 11 of LED HL Time Count" (VR4111 UM 24.2.1 p494)
   and LTS[6..0] "Values compared to bits 17 to 11" (24.2.2 p495), both scaling bit 11 as
   0.0625 seconds. */
constexpr uint32_t kHltcHz        = 32768u;
constexpr uint32_t kHltcUnitShift = 11u;
static_assert(kHltcHz == Vr41xxRtcxTicks::kHz);
constexpr uint64_t kUnitsPerHostSecond = kHltcHz >> kHltcUnitShift;

/* LEDCNTREG D1 LEDSTOP enables blink auto-stop and D0 LEDENABLE starts blinking; at auto-stop
   counter zero the unit clears both and sets LEDINT (VR4111 UM 24.3 p499). */
constexpr uint16_t kCntStop = 0x0002u;

/* LEDINT "Generates an interrupt request for the ICU unit" (VR4111 UM 24.3 p499); LEDINTR is
   D[1] of SYSINT2REG 0x0B000200 (VR4111 UM 15.2.15 p345). */
constexpr uint16_t kIcuLedintBit = 0x0002u;

/* LEDCNTREG: D1 LEDSTOP | D0 LEDENABLE R/W, D15:2 reserved read-0; RTCRST = LEDSTOP(1)/
   LEDENABLE(0), other resets retain (VR4111 UM 24.2.3 p496 / VR4102 UM 23.2.3 p456 /
   VR4131 UM 19.2.3 p361). */
constexpr uint16_t kCntMask    = 0x0003u;
constexpr uint16_t kCntEnable  = 0x0001u;
constexpr uint16_t kCntPowerOn = 0x0002u;

/* LEDHTSREG on-time D[4:0], LEDLTSREG off-time D[6:0], both in 0.0625 s units,
   RTCRST 1 s and 2 s respectively (VR4111 UM 24.2.1 p494 + 24.2.2 p495 /
   VR4102 UM 23.2.1 p454 + 23.2.2 p455 / VR4131 UM 19.2.1 p359 + 19.2.2 p360). */
constexpr uint16_t kHtsWritable = 0x001Fu;
constexpr uint16_t kLtsWritable = 0x007Fu;
constexpr uint16_t kHtsPowerOn  = 0x0010u;
constexpr uint16_t kLtsPowerOn  = 0x0020u;

/* LEDASTCREG D[15:0] R/W, RTCRST and other resets 1,200 (VR4111 UM 24.2.4 p497 /
   VR4102 UM 23.2.4 p457 / VR4131 UM 19.2.4 p362). */
constexpr uint16_t kAstcPowerOn = 0x04B0u;

/* LEDINTREG D0 LEDINT R/W "Cleared to 0 when 1 is written",
   D15:1 reserved read-0, both reset rows 0 (VR4111 UM 24.2.5 p498 / VR4102 UM 23.2.5 p458 /
   VR4131 UM 19.2.5 p363). */
constexpr uint16_t kIntLedint = 0x0001u;

constexpr WidgetRgb kClrLit{78, 201, 90};
constexpr WidgetRgb kClrDark{50, 55, 50};
constexpr WidgetRgb kClrRim{90, 95, 90};
constexpr WidgetRgb kClrRimBlink{120, 230, 130};

template <const std::string_view& Soc, uint32_t Base, Vr41xxRtcxDomain kClock>
class Vr41xxLedBase : public Peripheral, public HostWidget {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == Soc;
    }
    void OnReady() override {
        clock_ = &emu_.Get<GuestCycleClock>();
        event_ = clock_->Add([this] {
            std::lock_guard<std::mutex> lk(state_mutex_);
            UpdateLocked();
        });
        if constexpr (kClock == Vr41xxRtcxDomain::Peripheral) {
            rtcx_.Attach([this] {
                std::lock_guard<std::mutex> lk(state_mutex_);
                UpdateLocked();
            });
        } else {
            rtcx_.Attach();
            emu_.Get<GuestDeepSleep>().RegisterParkClock([this] {
                std::lock_guard<std::mutex> lk(state_mutex_);
                UpdateLocked();
            });
        }
        clock_->RegisterRateListener([this] {
            std::lock_guard<std::mutex> lk(state_mutex_);
            rtcx_.Rescale();
            ArmAutoStopLocked();
        });
        blink_anchor_tick_ = rtcx_.Now();
        second_ = emu_.TryGet<Vr41xxLedSecondElement>();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<HostWidgetRegistry>().Register(this);
        emu_.Get<GuestDeepSleep>().RegisterSleepEntryListener([this] {
            std::lock_guard<std::mutex> lk(state_mutex_);
            ApplyOtherResetLocked();
        });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            std::lock_guard<std::mutex> lk(state_mutex_);
            if (kind == ResetLineKind::Rtc) {
                cnt_ = kCntPowerOn;
                hts_ = kHtsPowerOn;
                lts_ = kLtsPowerOn;
            }
            ApplyOtherResetLocked();
        });
    }

    uint32_t MmioBase() const override { return Base; }
    /* The unit decodes a 0x20 window: "0x0B00 025F to 0x0B00 0240 LED" (VR4111 UM Table 6-10
       p170) and "0x0F00 019F to 0x0F00 0180 LED" (VR4131 UM Table 3-6 p82). */
    uint32_t MmioSize() const override { return 0x20u; }

    uint16_t ReadHalf(uint32_t addr) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (addr - Base) {
            case kOffHts: return hts_;
            case kOffLts: return lts_;
            case kOffHltcl:
            case kOffHltch: return kHltcUnstated;
            case kOffCnt: return cnt_;
            /* LEDASTCREG "The set value is read during a read" (VR4111 UM 24.2.4 p497). */
            case kOffAstc: return astc_;
            /* casio_cassiopeia_em500_ppc2000 gwes.exe sub_74CBC @0x74D0E "jalx sub_1C138",
               whose body is "*a1 = a2; return *a3;" with a3 = LEDINTREG. */
            case kOffInt: return int_;
            default: HaltUnsupportedAccess("LED ReadHalf", addr, 0);
        }
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        EvaluateLocked();
        switch (addr - Base) {
            case kOffHts:
                hts_ = static_cast<uint16_t>(value & kHtsWritable);
                NoteTimeChangeLocked("LEDHTSREG", value);
                break;
            case kOffLts:
                lts_ = static_cast<uint16_t>(value & kLtsWritable);
                NoteTimeChangeLocked("LEDLTSREG", value);
                break;
            case kOffHltcl:
            case kOffHltch:
                break;
            case kOffCnt:
                WriteCntLocked(static_cast<uint16_t>(value & kCntMask));
                break;
            case kOffAstc: WriteAstcLocked(value); break;
            case kOffInt:
                int_ = static_cast<uint16_t>(int_ & ~(value & kIntLedint));
                break;
            default: HaltUnsupportedAccess("LED WriteHalf", addr, value);
        }
        UpdateLocked();
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("LED WriteByte", addr, value);
    }
    /* casio_cassiopeia_e55 gwes.exe sub_AC1EC @0xAC244 "sw $t6, 0x240($t7)" and @0xAC250
       "sw $zero, 0x244($t8)"; nec_mobilepro_700_ce2 leddrv.dll sub_153054C @0x15305D8
       "sw $t3, 0x240($t4)". */
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - Base;
        if (off != kOffHts && off != kOffHltcl)
            HaltUnsupportedAccess("LED WriteWord", addr, value);
        WriteWordAsHalves(addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("LED ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("LED ReadWord", addr, 0); }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        const uint64_t  elapsed = BlinkTicksLocked();
        const CountFold fold    = (cnt_ & kCntEnable) ? FoldedLocked(PairIndexAt(elapsed))
                                                      : CountFold{cnt_, int_, astc_remaining_};
        w.Write("cnt", fold.cnt); w.Write("hts", hts_); w.Write("lts", lts_); w.Write("astc", astc_); w.Write("int", fold.intr);
        w.Write("blink_elapsed", elapsed);
        w.Write("astc_remaining", fold.remaining);
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        r.Read("cnt", cnt_); r.Read("hts", hts_); r.Read("lts", lts_); r.Read("astc", astc_); r.Read("int", int_);
        uint64_t blink_elapsed = 0;
        r.Read("blink_elapsed", blink_elapsed); r.Read("astc_remaining", astc_remaining_);
        rtcx_.Rebase();
        ReanchorBlinkLocked(blink_elapsed);
        last_pair_index_ = (cnt_ & kCntEnable) ? PairIndexLocked() : 0;
    }
    void PostRestore() override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        UpdateLocked();
    }

    std::string WidgetName() const override { return second_ ? second_->PackageName() : "Notification LED"; }
    WidgetGroup Group() const override { return WidgetGroup::Indicator; }
    std::string Tooltip() const override {
        uint16_t cnt, hts, lts;
        { std::lock_guard<std::mutex> lk(state_mutex_); cnt = cnt_; hts = hts_; lts = lts_; }
        char buf[96];
        if (cnt & kCntEnable)
            std::snprintf(buf, sizeof(buf), "Notification LED: blinking (on %ums / off %ums)",
                          static_cast<unsigned>(hts) * 125u / 2u,
                          static_cast<unsigned>(lts) * 125u / 2u);
        else
            std::snprintf(buf, sizeof(buf), "Notification LED: off");
        if (!second_) return buf;
        return std::string(buf) + "\n" + second_->ElementName() + (second_->Lit() ? ": on" : ": off");
    }
    WidgetIcon Icon() const override {
        constexpr int kR = 5;
        return LedIcon{Fill(), draw_blink_ ? kClrRimBlink : kClrRim, kR};
    }
    bool PollDirty() override {
        uint16_t cnt, hts, lts;
        { std::lock_guard<std::mutex> lk(state_mutex_); cnt = cnt_; hts = hts_; lts = lts_; }
        const bool blink = (cnt & kCntEnable) != 0u;
        const bool lit   = blink && (HostSteadyMicros() / 1000u * kUnitsPerHostSecond / 1000u) %
                                        (static_cast<uint64_t>(hts) + lts) < hts;
        const bool second_lit = second_ && second_->Lit();
        if (lit == draw_lit_ && blink == draw_blink_ && second_lit == draw_second_lit_) return false;
        draw_lit_        = lit;
        draw_blink_      = blink;
        draw_second_lit_ = second_lit;
        return true;
    }

private:
    static uint8_t AddChannel(uint8_t a, uint8_t b) {
        return static_cast<uint8_t>(std::min(255, static_cast<int>(a) + b));
    }
    WidgetRgb Fill() const {
        if (!draw_second_lit_) return draw_lit_ ? kClrLit : kClrDark;
        const WidgetRgb second = second_->LitColor();
        if (!draw_lit_) return second;
        return {AddChannel(kClrLit.r, second.r), AddChannel(kClrLit.g, second.g), AddChannel(kClrLit.b, second.b)};
    }

    uint64_t BlinkTicksLocked() {
        return blink_base_ + (rtcx_.Now() - blink_anchor_tick_);
    }
    void ReanchorBlinkLocked(uint64_t elapsed) {
        blink_base_        = elapsed;
        blink_anchor_tick_ = rtcx_.Now();
    }

    uint64_t PairIndexAt(uint64_t elapsed) const {
        return (elapsed >> kHltcUnitShift) / (hts_ + lts_);
    }
    uint64_t PairIndexLocked() {
        return PairIndexAt(BlinkTicksLocked());
    }

    void ApplyOtherResetLocked() {
        astc_ = kAstcPowerOn;
        int_  = 0;
        if (cnt_ & kCntEnable) ArmBlinkLocked();
        else                   astc_remaining_ = 0;
        UpdateLocked();
    }

    void ArmAutoStopLocked() {
        if (!(cnt_ & kCntEnable) || !(cnt_ & kCntStop) || astc_remaining_ == 0u) {
            clock_->Disarm(event_);
            return;
        }
        const uint64_t period = static_cast<uint64_t>(hts_) + lts_;
        const uint64_t stop   = (last_pair_index_ + astc_remaining_) * period;
        rtcx_.ArmAt(event_, blink_anchor_tick_ + ((stop << kHltcUnitShift) - blink_base_));
    }

    void UpdateLocked() {
        EvaluateLocked();
        DriveIcuLocked();
        ArmAutoStopLocked();
    }
    void ArmBlinkLocked() {
        last_pair_index_ = PairIndexLocked();
        astc_remaining_  = astc_;
    }

    /* "This register is a 16-bit down counter that sets the number of ON/OFF times prior to
       automatic stopping of LED activation" and "Setting a zero to this register is
       prohibited" (VR4111 UM 24.2.4 p497). */
    void WriteAstcLocked(uint16_t value) {
        astc_ = value;
        if (!(cnt_ & kCntEnable)) return;
        if (!value)
            emu_.Get<Fatal>().Die("VR41xx LED: LEDASTCREG written 0 while LEDENABLE is set");
        last_pair_index_ = PairIndexLocked();
        astc_remaining_  = value;
    }

    void NoteTimeChangeLocked(const char* reg, uint16_t value) {
        if (!(cnt_ & kCntEnable)) return;
        LOG(Periph, "[LED] %s=0x%04X written while LEDENABLE is set; blink timing undefined\n",
            reg, value);
        if (!hts_ || !lts_)
            emu_.Get<Fatal>().Die("VR41xx LED: %s=0x%04X leaves LEDHTSREG=0x%04X "
                                  "LEDLTSREG=0x%04X while LEDENABLE is set; zero is prohibited",
                                  reg, value, hts_, lts_);
        last_pair_index_ = PairIndexLocked();
    }

    void WriteCntLocked(uint16_t value) {
        const bool was_enabled = (cnt_ & kCntEnable) != 0u;
        const bool now_enabled = (value & kCntEnable) != 0u;
        if (now_enabled && !was_enabled) {
            if (!hts_ || !lts_ || !astc_)
                emu_.Get<Fatal>().Die("VR41xx LED: LEDENABLE set with LEDHTSREG=0x%04X "
                                      "LEDLTSREG=0x%04X LEDASTCREG=0x%04X; zero is prohibited",
                                      hts_, lts_, astc_);
            cnt_ = value;
            ReanchorBlinkLocked(0u);
            ArmBlinkLocked();
            return;
        }
        cnt_ = value;
    }

    struct CountFold {
        uint16_t cnt;
        uint16_t intr;
        uint16_t remaining;
    };
    CountFold FoldedLocked(uint64_t idx) const {
        CountFold fold{cnt_, int_, astc_remaining_};
        if (!(cnt_ & kCntEnable) || idx <= last_pair_index_) return fold;
        const uint64_t advance = idx - last_pair_index_;
        fold.remaining = advance >= astc_remaining_ ? 0u : static_cast<uint16_t>(astc_remaining_ - advance);
        if (astc_remaining_ == 0u || fold.remaining != 0u || !(cnt_ & kCntStop)) return fold;
        fold.cnt  = static_cast<uint16_t>(cnt_ & ~(kCntStop | kCntEnable));
        fold.intr = static_cast<uint16_t>(int_ | kIntLedint);
        return fold;
    }
    void EvaluateLocked() {
        if (!(cnt_ & kCntEnable)) return;
        const uint64_t  idx  = PairIndexLocked();
        const CountFold fold = FoldedLocked(idx);
        cnt_             = fold.cnt;
        int_             = fold.intr;
        astc_remaining_  = fold.remaining;
        last_pair_index_ = idx;
    }
    void DriveIcuLocked() {
        emu_.Get<Vr41xxIcu>().SetSysint2Source(kIcuLedintBit, (int_ & kIntLedint) != 0);
    }

    mutable std::mutex state_mutex_;

    uint16_t cnt_   = kCntPowerOn;
    uint16_t hts_   = kHtsPowerOn;
    uint16_t lts_   = kLtsPowerOn;
    uint16_t astc_  = kAstcPowerOn;
    uint16_t int_   = 0;

    uint64_t blink_base_ = 0;

    uint64_t last_pair_index_ = 0;
    uint16_t astc_remaining_  = 0;

    GuestCycleClock*        clock_            = nullptr;
    GuestCycleClock::Event* event_            = nullptr;
    Vr41xxRtcxTicks         rtcx_{emu_, kClock};
    uint64_t                blink_anchor_tick_ = 0;

    Vr41xxLedSecondElement* second_ = nullptr;

    bool draw_lit_        = false;
    bool draw_blink_      = false;
    bool draw_second_lit_ = false;
};

}  /* namespace cerf_vr41xx_led_detail */

}
