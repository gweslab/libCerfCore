#pragma once

#include "../freescale_wdog_impl.h"

#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_ccm.h"
#include "imx6_iomuxc.h"
#include "imx6_id.h"

#include <cstdint>

namespace cerfcore {

namespace cerf_imx6_wdog_detail {

using cerf_freescale_wdog_detail::FreescaleWdogBase;
using cerf_freescale_wdog_detail::kWcrReset;
using cerf_freescale_wdog_detail::kWcrWda;
using cerf_freescale_wdog_detail::kWcrWdbg;
using cerf_freescale_wdog_detail::kWcrWde;
using cerf_freescale_wdog_detail::kWcrWdzst;
using cerf_freescale_wdog_detail::kWicr;
using cerf_freescale_wdog_detail::kWmcr;
using cerf_freescale_wdog_detail::WdogResetSource;

constexpr uint16_t kWcrWdw          = 1u << 7;
constexpr uint16_t kWcrWdt          = 1u << 3;
constexpr uint16_t kWcrWritable     = 0xFFBFu;
constexpr uint16_t kWcrWriteOnce    = kWcrWdw | kWcrWdbg | kWcrWdzst;
constexpr uint16_t kWcrWriteOneOnce = kWcrWde | kWcrWdt;

constexpr uint16_t kWrsrSftw = 1u << 0;
constexpr uint16_t kWrsrTout = 1u << 1;
constexpr uint16_t kWrsrPor  = 1u << 4;

constexpr uint16_t kWicrWie      = 1u << 15;
constexpr uint16_t kWicrWritable = 0xC0FFu;
constexpr uint16_t kWicrReset    = 0x0004u;

constexpr uint16_t kWmcrPde = 1u << 0;
constexpr uint16_t kWmcrReset = kWmcrPde;

template <uint32_t Base, uint32_t Unit> class Imx6WdogBase : public FreescaleWdogBase<Base, SocId::Imx6dq> {
public:
    using Parent = FreescaleWdogBase<Base, SocId::Imx6dq>;
    using Parent::Parent;

    void OnReady() override {
        Parent::OnReady();
        power_down_event_ = clock_->Add([this] { OnPowerDown(); });
        StartPowerDown(clock_->Cycles());
    }

    bool ShouldRegister() override {
        auto* bd = this->emu_.template TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }

protected:
    using Parent::emu_;
    using Parent::clock_;
    using Parent::wcr_;
    using Parent::wsr_;
    using Parent::wrsr_;

    uint64_t CkilHz() const override { return Imx6Ccm::kCkilHz; }
    uint16_t WcrWritable() const override { return kWcrWritable; }
    uint16_t WcrWriteOnce() const override { return kWcrWriteOnce; }
    uint16_t WcrWriteOneOnce() const override { return kWcrWriteOneOnce; }

    bool SuspendedIn(FreescaleLowPowerMode mode) const override {
        switch (mode) {
            case FreescaleLowPowerMode::kRun:  return false;
            case FreescaleLowPowerMode::kWait: return (wcr_ & kWcrWdw) != 0u;
            case FreescaleLowPowerMode::kDoze:
            case FreescaleLowPowerMode::kStop: return (wcr_ & kWcrWdzst) != 0u;
            default:                           break;
        }
        emu_.template Get<Fatal>().Die("Imx6Wdog %08X: low-power mode %u is not an i.MX6 mode",
                                       Base, static_cast<unsigned>(mode));
    }

    bool SuspendStopsPrescaler() const override { return true; }

    void OnWcrChange() override {
        if ((wcr_ & kWcrWda) == 0u) {
            AssertWdogB();
        }
    }

    void OnCounterTimeout() override {
        LOG(SocWdt, "Imx6Wdog %08X: time-out WCR=0x%04X\n", Base, wcr_);
        this->RaiseReset(WdogResetSource::kTimeout);
        if ((wcr_ & kWcrWdt) != 0u) AssertWdogB();
    }

    void OnPowerOn(uint64_t) override {
        wcr_  = kWcrReset;
        wsr_  = 0u;
        wrsr_ = kWrsrPor;
        RestartUnit();
    }

    void ResetRegisters(ResetLineKind kind, WdogResetSource source, uint64_t now) override {
        const bool por = kind == ResetLineKind::Rtc;
        wcr_ = static_cast<uint16_t>(kWcrReset | (por ? 0u : (wcr_ & kWcrWdt)));
        wsr_ = 0u;
        if (por) wrsr_ = kWrsrPor;
        else if (source == WdogResetSource::kTimeout) wrsr_ = kWrsrTout;
        else if (source == WdogResetSource::kSoftware) wrsr_ = kWrsrSftw;
        RestartUnit();
        StartPowerDown(now);
    }

    bool ResetBySiblingUnit() const override { return true; }
    bool KeepStateOnResume() const override { return true; }
    void BeforeRaiseReset(WdogResetSource) override { AssertResetBDebPad(); }

    bool ReadExtra(uint32_t off, uint16_t& value) override {
        if (off == kWicr) { value = wicr_; return true; }
        if (off == kWmcr) { value = wmcr_; return true; }
        return false;
    }

    bool WriteExtra(uint32_t off, uint16_t value) override {
        if (off == kWicr) { WriteWicr(value); return true; }
        if (off == kWmcr) { WriteWmcr(value); return true; }
        return false;
    }

    void SaveExtra(StateWriter& w) override {
        w.Write("wicr", wicr_);
        w.Write<uint8_t>("wicr_written", wicr_written_ ? 1u : 0u);
        w.Write("wmcr", wmcr_);
        w.Write<uint8_t>("power_down_running", power_down_running_ ? 1u : 0u);
        const uint64_t now = clock_->Cycles();
        const int64_t remaining_ns = power_down_running_ && power_down_deadline_ > now
                                         ? clock_->CyclesToNs(power_down_deadline_ - now)
                                         : 0;
        w.Write("power_down_remaining_ns", remaining_ns);
    }

    void RestoreExtra(StateReader& r) override {
        uint8_t written = 0u;
        uint8_t running = 0u;
        r.Read("wicr", wicr_);
        r.Read("wicr_written", written);
        r.Read("wmcr", wmcr_);
        r.Read("power_down_running", running);
        r.Read("power_down_remaining_ns", power_down_remaining_ns_);
        wicr_written_ = written != 0u;
        power_down_running_ = running != 0u;
        clock_->Disarm(power_down_event_);
    }

    void PostRestoreExtra() override {
        if (!power_down_running_ || (wmcr_ & kWmcrPde) == 0u) return;
        const uint64_t now = clock_->Cycles();
        power_down_deadline_ = now + clock_->NsToCycles(power_down_remaining_ns_);
        clock_->Arm(power_down_event_, power_down_deadline_);
    }

private:
    void AssertWdogB() {
        const Imx6WdogPadMux* pad =
            emu_.template Get<Imx6Iomuxc>().WdogPadMux(Unit, Imx6WdogPadSignal::WdogB);
        if (pad) {
            emu_.template Get<Fatal>().Die("WDOG%u_B asserted on pad %s: board effect not modelled",
                                           Unit, pad->pad_name);
        }
        LOG(SocWdt, "Imx6Wdog %08X: watchdog output asserted; no pad muxed, no external effect\n", Base);
    }

    void AssertResetBDebPad() {
        const Imx6WdogPadMux* pad =
            emu_.template Get<Imx6Iomuxc>().WdogPadMux(Unit, Imx6WdogPadSignal::ResetBDeb);
        if (pad) {
            emu_.template Get<Fatal>().Die("WDOG%u_RESET_B_DEB asserted on pad %s: board effect not modelled",
                                           Unit, pad->pad_name);
        }
    }

    void RestartUnit() {
        wicr_         = kWicrReset;
        wicr_written_ = false;
        wmcr_         = kWmcrReset;
        power_down_running_ = true;
    }

    void WriteWicr(uint16_t value) {
        if ((value & ~kWicrWritable) != 0u) {
            emu_.template Get<Fatal>().Die("Imx6Wdog %08X: WICR write 0x%04X sets a reserved "
                                           "bit", Base, value);
        }
        if ((value & kWicrWie) != 0u) {
            emu_.template Get<Fatal>().Die("Imx6Wdog %08X: WICR write 0x%04X enables the "
                                           "pre-time-out interrupt; it is not modelled", Base,
                                           value);
        }
        const uint16_t fields = static_cast<uint16_t>(value & 0x80FFu);
        if (wicr_written_ && fields != (wicr_ & 0x80FFu)) {
            emu_.template Get<Fatal>().Die("Imx6Wdog %08X: WICR write 0x%04X changes a "
                                           "write-once field of 0x%04X", Base, value, wicr_);
        }
        wicr_         = fields;
        wicr_written_ = true;
    }

    void WriteWmcr(uint16_t value) {
        if ((value & ~kWmcrPde) != 0u) {
            emu_.template Get<Fatal>().Die("Imx6Wdog %08X: WMCR write 0x%04X sets a reserved "
                                           "bit", Base, value);
        }
        if ((wmcr_ & kWmcrPde) == 0u && (value & kWmcrPde) != 0u) {
            emu_.template Get<Fatal>().Die("Imx6Wdog %08X: WMCR write 0x%04X re-enables the write-once power-down counter", Base, value);
        }
        wmcr_ = value & kWmcrPde;
        if (wmcr_ == 0u) {
            power_down_running_ = false;
            power_down_deadline_ = 0u;
            clock_->Disarm(power_down_event_);
        }
    }

    void StartPowerDown(uint64_t now) {
        if (!power_down_event_ || (wmcr_ & kWmcrPde) == 0u) return;
        power_down_running_ = true;
        power_down_deadline_ = now + clock_->NsToCycles(16000000000LL);
        clock_->Arm(power_down_event_, power_down_deadline_);
    }

    void OnPowerDown() {
        if (!power_down_running_) {
            emu_.template Get<Fatal>().Die("Imx6Wdog %08X: power-down event fired while disabled", Base);
        }
        power_down_running_ = false;
        power_down_deadline_ = 0u;
        AssertWdogB();
    }

    uint16_t wicr_         = kWicrReset;
    uint16_t wmcr_         = kWmcrReset;
    bool     wicr_written_ = false;
    GuestCycleClock::Event* power_down_event_ = nullptr;
    int64_t power_down_remaining_ns_ = 0;
    uint64_t power_down_deadline_ = 0u;
    bool power_down_running_ = true;
};

}

}
