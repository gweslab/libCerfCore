#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "vr41xx_rtcx_ticks.h"

#include <cstdint>
#include <mutex>
#include <string_view>

namespace cerfcore {

namespace cerf_vr41xx_dsu_detail {

constexpr uint32_t kBase = 0x0B0000E0u;
constexpr uint32_t kSize = 0x20u;

constexpr uint32_t kOffCnt = 0x00u;
constexpr uint32_t kOffSet = 0x02u;
constexpr uint32_t kOffClr = 0x04u;

constexpr uint16_t kCntDswen   = 0x0001u;
constexpr uint16_t kSetDedtime = 0x000Fu;
constexpr uint16_t kClrDswclr  = 0x0001u;

template <const std::string_view& Soc>
class Vr41xxDsuBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == Soc;
    }

    void OnReady() override {
        clock_ = &emu_.Get<GuestCycleClock>();
        event_ = clock_->Add([this] { OnDeadline(); });
        rtcx_.Attach();
        clock_->RegisterRateListener([this] {
            std::lock_guard<std::mutex> lk(mtx_);
            rtcx_.Rescale();
            ArmLocked();
        });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            std::lock_guard<std::mutex> lk(mtx_);
            ResetLocked();
        });
        emu_.Get<GuestDeepSleep>().RegisterSleepEntryListener([this] {
            std::lock_guard<std::mutex> lk(mtx_);
            ResetLocked();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        std::lock_guard<std::mutex> lk(mtx_);
        switch (addr - kBase) {
            case kOffCnt: return cnt_;
            case kOffSet: return set_;
            default: HaltUnsupportedAccess("VR41xx DSU ReadHalf", addr, 0);
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        std::lock_guard<std::mutex> lk(mtx_);
        switch (addr - kBase) {
            case kOffCnt: WriteCntLocked(addr, value); return;
            case kOffSet: WriteSetLocked(addr, value); return;
            case kOffClr: WriteClrLocked(addr, value); return;
            default: HaltUnsupportedAccess("VR41xx DSU WriteHalf", addr, value);
        }
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mtx_);
        w.Write("cnt", cnt_);
        w.Write("set", set_);
        w.Write("elapsed", ElapsedLocked());
        w.Write<uint8_t>("started", started_ ? 1u : 0u);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mtx_);
        uint64_t elapsed = 0;
        r.Read("cnt", cnt_);
        r.Read("set", set_);
        r.Read("elapsed", elapsed);
        uint8_t started = 0;
        r.Read("started", started);
        started_ = started != 0u;
        rtcx_.Rebase();
        anchor_ = rtcx_.Now() - elapsed;
    }

    void PostRestore() override {
        std::lock_guard<std::mutex> lk(mtx_);
        ArmLocked();
    }

private:
    bool Enabled() const { return (cnt_ & kCntDswen) != 0u; }

    uint64_t PeriodTicks() const { return static_cast<uint64_t>(set_) * Vr41xxRtcxTicks::kHz; }

    uint64_t ElapsedLocked() { return Enabled() ? rtcx_.Now() - anchor_ : 0u; }

    void WriteCntLocked(uint32_t addr, uint16_t value) {
        if ((value & ~kCntDswen) != 0u) HaltUnsupportedAccess("VR41xx DSU DSUCNTREG reserved bits", addr, value);
        const bool was_enabled = Enabled();
        if ((value & kCntDswen) != 0u && !was_enabled) {
            if (started_) {
                emu_.Get<Fatal>().Die("VR41xx DSU: DSWEN set again after a disable with no reset between");
            }
            started_ = true;
            anchor_  = rtcx_.Now();
        }
        cnt_ = value;
        ArmLocked();
    }

    void WriteSetLocked(uint32_t addr, uint16_t value) {
        if ((value & ~kSetDedtime) != 0u) HaltUnsupportedAccess("VR41xx DSU DSUSETREG reserved bits", addr, value);
        if (Enabled()) {
            emu_.Get<Fatal>().Die("VR41xx DSU: DSUSETREG write 0x%04X while DSWEN is set", value);
        }
        set_ = value;
        ArmLocked();
    }

    void WriteClrLocked(uint32_t addr, uint16_t value) {
        if ((value & ~kClrDswclr) != 0u) HaltUnsupportedAccess("VR41xx DSU DSUCLRREG reserved bits", addr, value);
        if ((value & kClrDswclr) != 0u) anchor_ = rtcx_.Now();
        ArmLocked();
    }

    void ArmLocked() {
        if (!Enabled()) {
            clock_->Disarm(event_);
            return;
        }
        if (set_ == 0u) {
            emu_.Get<Fatal>().Die("VR41xx DSU: DSWEN set with DSUSETREG DEDTIME 0");
        }
        rtcx_.ArmAt(event_, anchor_ + PeriodTicks());
    }

    void ResetLocked() {
        cnt_     = 0;
        set_     = 0;
        anchor_  = 0;
        started_ = false;
        clock_->Disarm(event_);
    }

    void OnDeadline() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!Enabled()) return;
            if (rtcx_.Now() - anchor_ < PeriodTicks()) {
                ArmLocked();
                return;
            }
        }
        emu_.Get<GuestCpuReset>().WatchdogReset();
    }

    std::mutex              mtx_;
    uint16_t                cnt_    = 0;
    uint16_t                set_    = 0;
    uint64_t                anchor_ = 0;
    bool                    started_ = false;
    GuestCycleClock*        clock_  = nullptr;
    GuestCycleClock::Event* event_  = nullptr;
    Vr41xxRtcxTicks         rtcx_{emu_, Vr41xxRtcxDomain::RtcIcuPmu};
};

}

}
