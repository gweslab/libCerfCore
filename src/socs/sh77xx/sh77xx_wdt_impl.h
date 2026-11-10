#pragma once

#include <cstdint>
#include <string_view>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "../cycle_anchored_counter.h"
#include "../rated_tick_count.h"
#include "../reg_window_impl.h"
#include "sh77xx_cpg.h"
#include "sh77xx_frqcr.h"
#include "sh77xx_wdt.h"

namespace cerfcore {

namespace sh77xx_wdt {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr uint32_t kBase = 0x1FFFFF84u;

inline constexpr uint32_t kWtcnt = 0u;
inline constexpr uint32_t kWtcsr = 1u;

inline constexpr uint16_t kWtcntKey = 0x5Au;
inline constexpr uint16_t kWtcsrKey = 0xA5u;
inline constexpr uint16_t kData     = 0x00FFu;
inline constexpr uint16_t kWtcsrTme = 0x0080u;
inline constexpr uint16_t kWtcsrCks = 0x0007u;

inline constexpr uint64_t kCksDivision[8]      = {1u, 4u, 16u, 32u, 64u, 256u, 1024u, 4096u};
inline constexpr uint64_t kStandbyReentryCount = 0x80u;

constexpr RegSpec Register(uint16_t fatal_on_set) {
    return RegSpec{ReadKind::kFatal, WriteKind::kStored, kData, 0u, fatal_on_set, OtherReset::kRetain};
}

constexpr RegWindowModel WindowOf() {
    RegWindowModel w{kBase, 4u, 2u, false, {}};
    w.reg[kWtcnt] = Register(0u);
    w.reg[kWtcsr] = Register(kWtcsrTme);
    return w;
}

inline constexpr RegWindowModel kWindow = WindowOf();

template <const std::string_view& Soc>
class Sh77xxWdtBase : public RegWindowBase<Soc, kWindow, Sh77xxWdt> {
    using Base = RegWindowBase<Soc, kWindow, Sh77xxWdt>;

public:
    using Base::Base;

    void OnReady() override {
        Base::OnReady();
        clock_ = &this->emu_.template Get<GuestCycleClock>();
        cpg_   = &this->emu_.template Get<Sh77xxCpg>();
        event_ = clock_->Add([this] { OnStandbyReentry(); });
        clock_->RegisterRateListener([this] { Retime(); });
        this->emu_.template Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { Disarm(); });
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint16_t key = (addr - kBase) / 2u == kWtcnt ? kWtcntKey : kWtcsrKey;
        if ((value >> 8) != key) this->HaltUnsupportedAccess("WriteHalf without the register key", addr, value);
        if (counting_) {
            this->emu_.template Get<Fatal>().Die("Sh77xxWdt: write of 0x%04X at 0x%08X while the WDT counts after a standby exit",
                                                 value, addr);
        }
        Base::WriteHalf(addr, static_cast<uint16_t>(value & kData));
    }

    void CountAfterStandby() override {
        counting_    = true;
        count_frqcr_ = Frqcr();
        pclk_.SetRate(clock_->ClockRate(), cpg_->PeripheralRate(count_frqcr_));
        pclk_.Start(clock_->Cycles());
        Arm();
    }

    void StopStandbyCount() override {
        if (!counting_) return;
        RequireCountClock();
        Disarm();
    }

    void SaveState(StateWriter& w) override {
        Base::SaveState(w);
        const uint64_t now = clock_->Cycles();
        w.Write("standby_count", counting_);
        w.Write("standby_count_frqcr", count_frqcr_);
        w.Write("standby_count_ticks", counting_ ? pclk_.TicksAt(now) : uint64_t{0u});
        (counting_ ? pclk_.TickPhaseAt(now) : CounterPhase{}).Save(w, "standby_count_phase");
    }

    void RestoreState(StateReader& r) override {
        Base::RestoreState(r);
        r.Read("standby_count", counting_);
        r.Read("standby_count_frqcr", count_frqcr_);
        r.Read("standby_count_ticks", restored_ticks_);
        restored_phase_.Restore(r, "standby_count_phase");
    }

    void PostRestore() override {
        if (!counting_) {
            clock_->Disarm(event_);
            return;
        }
        pclk_.SetRate(clock_->ClockRate(), cpg_->PeripheralRate(count_frqcr_));
        pclk_.PlaceAtPhase(clock_->Cycles(), restored_ticks_, restored_phase_);
        Arm();
    }

private:
    uint16_t Frqcr() {
        const Sh77xxFrqcr* frqcr = this->emu_.template TryGet<Sh77xxFrqcr>();
        if (frqcr == nullptr) {
            this->emu_.template Get<Fatal>().Die("Sh77xxWdt: the count after a standby exit on %.*s needs FRQCR, "
                                                 "and no FRQCR model exists for it",
                                                 static_cast<int>(Soc.size()), Soc.data());
        }
        return frqcr->Value();
    }

    void RequireCountClock() {
        const uint16_t frqcr = Frqcr();
        if (frqcr == count_frqcr_) return;
        this->emu_.template Get<Fatal>().Die(
            "Sh77xxWdt: FRQCR changed from 0x%04X to 0x%04X while the WDT counts after a standby exit", count_frqcr_,
            frqcr);
    }

    void Arm() {
        const uint64_t ticks = kStandbyReentryCount * kCksDivision[this->StoredReg(kWtcsr) & kWtcsrCks];
        clock_->Arm(event_, pclk_.CycleOfTick(ticks));
    }

    void Disarm() {
        counting_ = false;
        clock_->Disarm(event_);
    }

    void Retime() {
        if (!counting_) return;
        RequireCountClock();
        pclk_.Rescale(clock_->Cycles(), clock_->ClockRate(), cpg_->PeripheralRate(count_frqcr_));
        Arm();
    }

    void OnStandbyReentry() {
        RequireCountClock();
        this->emu_.template Get<Fatal>().Die(
            "Sh77xxWdt: WTCNT reached H'80 after a standby exit with STBCR.STBY set (WTCSR CKS %u)",
            this->StoredReg(kWtcsr) & kWtcsrCks);
    }

    GuestCycleClock*        clock_          = nullptr;
    GuestCycleClock::Event* event_          = nullptr;
    Sh77xxCpg*              cpg_            = nullptr;
    RatedTickCount          pclk_{this->emu_};
    CounterPhase            restored_phase_{};
    uint64_t                restored_ticks_ = 0u;
    uint16_t                count_frqcr_    = 0u;
    bool                    counting_       = false;
};

}

}
