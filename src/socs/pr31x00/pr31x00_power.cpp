#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../execution/engine_bridge.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "pr31x00_clock.h"
#include "pr31x00_intc.h"
#include "pr31x00_power_inputs.h"
#include "pr31x00_reset_line.h"
#include "pr31x00_rtc.h"

#include <atomic>
#include <cstdint>
#include <optional>

namespace cerfcore {

namespace {

/* PR31x00 Power module. The single Power Control Register sits at offset $1C4 of
   the Internal Function Registers (PA 0x10C00000, TMPR3911/3912 Table 4.2.1);
   field layout and reset values per §12.3.1. */
constexpr uint32_t kBase = 0x10C001C4u;
constexpr uint32_t kSize = 0x4u;

constexpr uint32_t kOnButn    = 1u << 31;
constexpr uint32_t kPwrInt    = 1u << 30;
constexpr uint32_t kPwrOk     = 1u << 29;
constexpr uint32_t kStpTimerVal = 0xF000u;  /* STPTIMERVAL[3:0]<15:12> (§12.3.1) */
constexpr uint32_t kStpTimerValShift = 12u;
constexpr uint32_t kEnStpTimer = 1u << 11;  /* ENSTPTIMER (§12.3.1) */
constexpr uint32_t kForceShutDwn = 1u << 9;
constexpr uint32_t kStopCpu   = 1u << 4;
constexpr uint32_t kColdStart = 1u << 2;
constexpr uint32_t kPwrCs     = 1u << 1;
constexpr uint32_t kVccOn     = 1u << 0;

constexpr uint32_t kWritable = 0x1E00FFBFu;   /* VIDRF..DIVMOD, STPTIMERVAL..SELC2MS, BPDBVCC3..VCCON */
constexpr uint32_t kReadOnly = kOnButn | kPwrInt | kPwrOk;

/* STPTIMERINT = Interrupt Status 5 (set index 4) bit 28 (§8.3.5; NetBSD
   tx39icureg.h). Stop Timer §12.2.8; its 8 ms clock comes from the first 8-bit RTC
   ripple stage (§15.4.3 FREEZEPRE, §15.2.2, Figure 15.2.1). */
constexpr uint32_t kStpIntStatusSet = 4u;
constexpr uint32_t kStpTimerInt     = 1u << 28;
constexpr uint32_t kStpCounterMask  = 0xFu;

constexpr uint32_t kPowerUp   = kPwrCs | kVccOn | kForceShutDwn;
constexpr uint32_t kAfterPon  = kColdStart | kPowerUp;

class Pr31x00Power : public Peripheral, public ResetCauseLatch, public DeepSleepClockStop {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Pr31500 || soc == SocId::Pr31700;
    }
    void OnReady() override {
        intc_ = &emu_.Get<Pr31x00Intc>();
        power_inputs_ = emu_.TryGet<Pr31x00PowerInputs>();
        clock_ = &emu_.Get<GuestCycleClock>();
        rtc_   = &emu_.Get<Pr31x00Rtc>();
        stp_event_ = clock_->Add([this] { OnStopTimerMatch(); });
        rtc_->RegisterCountListener([this] { ArmStopTimerEvent(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().SetCauseLatch(this);
        emu_.Get<Pr31x00ResetLine>().RegisterResetListener([this] {
            ctl_.store((Ctl() & kStpTimerVal) | kAfterPon, std::memory_order_release);
            DisarmStopTimer();
        });
        auto& sleep = emu_.Get<GuestDeepSleep>();
        sleep.RegisterClockStopWaker(this);
        sleep.RegisterSleepEntryListener([this] {
            if (stp_armed_) {
                emu_.Get<Fatal>().Die("Pr31x00Power: power down with the Stop Timer counting; "
                                      "whether it counts in the Suspend State is not modelled");
            }
            if (intc_->SourceEnabledWithoutGlobalEnable()) {
                emu_.Get<Fatal>().Die("Pr31x00Power: power down with interrupt sources enabled "
                                      "and GLOBALEN clear; whether they wake the system is "
                                      "not modelled");
            }
            wake_at_entry_ = PowerUpRequested();
        });
        /* §12.2.4 (p.12-8): "if rising edge of the ONBUTN signal or an enabled
           interrupt occurs, and the PWROK signal is asserted, then the system
           will power up." */
        sleep.RegisterParkWakeSource([this] { return wake_at_entry_ || PowerUpRequested(); });
        sleep.RegisterParkWakeDue([this, &sleep] {
            return wake_at_entry_ ? sleep.SleptNs() : GuestDeepSleep::kNoParkWake;
        });
    }

    void OnPowerUp() override {
        ctl_.fetch_or(kPowerUp, std::memory_order_acq_rel);
    }

    void LatchColdReset() override {}
    void LatchWarmReset() override {}
    void LatchWatchdogReset() override {}
    void ReportOnlyWarmReset() override { ctl_.fetch_and(~kColdStart, std::memory_order_acq_rel); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        if (addr != kBase) {
            HaltUnsupportedAccess("PR31x00 Power ReadWord", addr, 0);
        }
        uint32_t sig = signals_;
        if (power_inputs_ && power_inputs_->PwrIntAsserted()) {
            sig |= kPwrInt;
        }
        return Ctl() | sig;
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr != kBase) {
            HaltUnsupportedAccess("PR31x00 Power WriteWord", addr, value);
        }
        const uint32_t prev = Ctl();
        const uint32_t next = value & kWritable;
        ctl_.store(next & ~kStopCpu, std::memory_order_release);
        emu_.Get<Pr31x00Clock>().SetPowerClockBits(next);

        /* TMPR3911 §12.2.8 p.12-10: "When the ENSTPTIMER control bit is not set the counter
           will be reset to zero. Once the ENSTPTIMER bit is set the counter will count up
           using an 8 ms clock as the input." */
        const bool was_stp = (prev & kEnStpTimer) != 0u;
        const bool now_stp = (next & kEnStpTimer) != 0u;
        if (now_stp && !was_stp) {
            StartStopTimer();
        } else if (was_stp && !now_stp) {
            DisarmStopTimer();
        } else if (was_stp && ((prev ^ next) & kStpTimerVal) != 0u) {
            MatchStopTimer();
        }

        /* TMPR3911 §12.3.1 p.12-14: "When powering down the system, [VCCON] must be
           cleared simultaneously with the PWRCS bit." */
        const uint32_t power_pair = next & (kVccOn | kPwrCs);
        if (power_pair != 0u && power_pair != (kVccOn | kPwrCs)) {
            emu_.Get<Fatal>().Die("Pr31x00Power: write 0x%08X leaves only one of VCCON and PWRCS set",
                                  value);
        }
        const bool was_on = (prev & (kVccOn | kPwrCs)) != 0u;
        const bool now_off = power_pair == 0u;
        const bool stop_cpu = (next & kStopCpu) != 0u;
        if (was_on && now_off) {
            if (stop_cpu) {
                emu_.Get<Fatal>().Die("Pr31x00Power: STOPCPU set in a power-down write 0x%08X",
                                      value);
            }
            emu_.Get<GuestDeepSleep>().Enter();
        }
        /* TMPR3911 §12.3.1 p.12-13: STOPCPU disables the CPU core clock, and "The bit
           is cleared whenever an enabled interrupt is set." */
        if (stop_cpu && intc_->SourceEnabledWithoutGlobalEnable()) {
            emu_.Get<Fatal>().Die("Pr31x00Power: STOPCPU with interrupt sources enabled and "
                                  "GLOBALEN clear; whether they clear it is not modelled");
        }
        if (stop_cpu) emu_.Get<EngineBridge>().Guest().EnterIdleWait();
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("PR31x00 Power ReadByte", addr, 0); }
    uint16_t ReadHalf (uint32_t addr) override { HaltUnsupportedAccess("PR31x00 Power ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 Power WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 Power WriteHalf", addr, v); }

    void SaveState(StateWriter& w) override {
        w.Write("ctl", Ctl()); w.Write("signals", signals_);
        w.Write<uint8_t>("stp_armed", stp_armed_ ? 1u : 0u);
        w.Write<uint32_t>("stp_count", stp_armed_ ? StopTimerCount() : 0u);
    }
    void RestoreState(StateReader& r) override {
        uint32_t ctl = 0;
        r.Read("ctl", ctl);
        ctl_.store(ctl, std::memory_order_release);
        r.Read("signals", signals_);
        uint8_t armed = 0;
        r.Read("stp_armed", armed);
        uint32_t count = 0;
        r.Read("stp_count", count);
        stp_armed_          = armed != 0u;
        stp_restored_count_ = count;
    }

    void PostRestore() override {
        if (stp_armed_) stp_base_ = rtc_->Tc0Carries() - stp_restored_count_;
        ArmStopTimerEvent();
    }

private:
    uint32_t Ctl() const { return ctl_.load(std::memory_order_acquire); }

    uint32_t StopTimerValue() const { return (Ctl() & kStpTimerVal) >> kStpTimerValShift; }

    uint32_t StopTimerCount() {
        return static_cast<uint32_t>((rtc_->Tc0Carries() - stp_base_) & kStpCounterMask);
    }

    /* §12.2.8: the counter is zero while ENSTPTIMER is clear and counts the 8 ms
       pulse once it is set. */
    void StartStopTimer() {
        stp_base_  = rtc_->Tc0Carries();
        stp_armed_ = true;
        MatchStopTimer();
    }

    /* §12.3.1 p.12-12: "When the Stop Timer is enabled the STPTIMERINT interrupt will be set
       when the counter is equal to the value set by these bits." */
    void MatchStopTimer() {
        if (StopTimerCount() == StopTimerValue()) intc_->SetPending(kStpIntStatusSet, kStpTimerInt);
        ArmStopTimerEvent();
    }

    void DisarmStopTimer() {
        stp_armed_ = false;
        clock_->Disarm(stp_event_);
    }

    void ArmStopTimerEvent() {
        if (!stp_armed_) {
            clock_->Disarm(stp_event_);
            return;
        }
        const uint64_t carries = rtc_->Tc0Carries();
        const uint32_t count   = static_cast<uint32_t>((carries - stp_base_) & kStpCounterMask);
        const uint64_t ahead   = ((StopTimerValue() - count - 1u) & kStpCounterMask) + 1u;
        const std::optional<uint64_t> match = rtc_->CycleOfTc0Carry(carries + ahead);
        if (match) {
            clock_->Arm(stp_event_, *match);
        } else {
            clock_->Disarm(stp_event_);
        }
    }

    /* §12.2.9: STPTIMERINT "is set whenever the Stop Timer Counter counts up to the
       value set by the STPTIMERVAL[3:0] control bits". */
    void OnStopTimerMatch() {
        intc_->SetPending(kStpIntStatusSet, kStpTimerInt);
        ArmStopTimerEvent();
    }

    bool PowerUpRequested() const {
        return (signals_ & kPwrOk) != 0u && intc_->EnabledInterruptPending();
    }

    std::atomic<uint32_t> ctl_{kAfterPon};

    uint32_t signals_ = kPwrOk;
    bool     wake_at_entry_ = false;

    Pr31x00Intc* intc_ = nullptr;
    Pr31x00PowerInputs* power_inputs_ = nullptr;

    GuestCycleClock*        clock_              = nullptr;
    Pr31x00Rtc*             rtc_                = nullptr;
    GuestCycleClock::Event* stp_event_          = nullptr;
    uint64_t                stp_base_           = 0;
    uint32_t                stp_restored_count_ = 0;
    bool                    stp_armed_          = false;

    static_assert((kWritable & kReadOnly) == 0u, "writable and read-only fields overlap");
};

}

REGISTER_SERVICE(Pr31x00Power);

}
