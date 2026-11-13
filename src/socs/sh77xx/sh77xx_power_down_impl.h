#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfengine/sh/sh_interrupt_channel.h>
#include <cerfengine/sh/sh_sleep_host.h>
#include "../../execution/engine_bridge.h"
#include "../reg_window_impl.h"
#include "sh77xx_standby_control.h"
#include "sh77xx_wdt.h"
#include <cerfcore/core/callback_list.h>

namespace cerfcore {

namespace sh77xx_power_down {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr uint32_t kStbcrPa  = 0x1FFFFF82u;
inline constexpr uint32_t kStbcr2Pa = 0x1FFFFF88u;

inline constexpr uint16_t kStbcrWritable  = 0x87u;
inline constexpr uint16_t kStbcrWriteZero = 0x78u;
inline constexpr uint16_t kStbcrStby      = 0x80u;

constexpr RegWindowModel ByteRegister(uint32_t pa, uint16_t writable, uint16_t write_zero, OtherReset other_reset) {
    RegWindowModel w{pa, 1u, 1u, false, {}, 1u};
    w.reg[0] = RegSpec{ReadKind::kStored, WriteKind::kStored, writable, 0u, write_zero, other_reset};
    return w;
}

constexpr RegWindowModel Stbcr(OtherReset other_reset) {
    return ByteRegister(kStbcrPa, kStbcrWritable, kStbcrWriteZero, other_reset);
}

constexpr RegWindowModel Stbcr2(uint16_t writable, uint16_t write_zero, OtherReset other_reset) {
    return ByteRegister(kStbcr2Pa, writable, write_zero, other_reset);
}

template <const std::string_view& Soc, const RegWindowModel& M, class Slot>
class ModuleStopRegister : public RegWindowBase<Soc, M, Slot> {
public:
    using RegWindowBase<Soc, M, Slot>::RegWindowBase;

    bool ModuleStopped(uint8_t mstp) const override { return (this->StoredReg(0u) & mstp) != 0u; }

    void RegisterChangeListener(typename Slot::ChangeListener listener) override {
        listeners_.Add(std::move(listener));
    }

protected:
    void BeforeStore(uint32_t, uint16_t) override { before_ = static_cast<uint8_t>(this->StoredReg(0u)); }

    void AfterStore(uint32_t) override {
        const uint8_t after = static_cast<uint8_t>(this->StoredReg(0u));
        if (after == before_) return;
        listeners_.Call(before_, after);
    }

private:
    CallbackList<uint8_t, uint8_t>             listeners_;
    uint8_t                                    before_ = 0u;
};

template <const std::string_view& Soc, const RegWindowModel& M>
class StandbyControlRegister : public ModuleStopRegister<Soc, M, Sh77xxStbcr>,
                               public cerfengine::ShSleepHost,
                               public DeepSleepClockStop {
public:
    using ModuleStopRegister<Soc, M, Sh77xxStbcr>::ModuleStopRegister;

    void OnReady() override {
        ModuleStopRegister<Soc, M, Sh77xxStbcr>::OnReady();
        channel_ = &this->emu_.template Get<EngineBridge>().Engine().template Get<cerfengine::ShInterruptChannel>();
        sleep_   = &this->emu_.template Get<GuestDeepSleep>();
        wdt_     = &this->emu_.template Get<Sh77xxWdt>();
        channel_->SetSleepHost(this);
        sleep_->RegisterClockStopWaker(this);
        sleep_->RegisterSleepEntryListener([this] { detected_ = channel_->StandbyWakePending(); });
        sleep_->RegisterParkWakeSource([this] { return Detect(); });
        sleep_->RegisterParkWakeDue([this] {
            return detected_ ? sleep_->SleptNs() : GuestDeepSleep::kNoParkWake;
        });
    }

    bool StandbySelected() const override { return (this->StoredReg(0u) & kStbcrStby) != 0u; }

    void EnterStandby() override {
        exited_ = false;
        sleep_->Enter();
    }

    bool StandbyExited() const override { return exited_; }

    void OnPowerUp() override {
        exited_ = Detect();
        if (exited_) wdt_->CountAfterStandby();
    }

protected:
    void AfterStore(uint32_t i) override {
        ModuleStopRegister<Soc, M, Sh77xxStbcr>::AfterStore(i);
        if (!StandbySelected()) wdt_->StopStandbyCount();
    }

private:
    bool Detect() {
        if (!detected_) detected_ = channel_->StandbyWakePending();
        return detected_;
    }

    cerfengine::ShInterruptChannel* channel_  = nullptr;
    GuestDeepSleep*                 sleep_    = nullptr;
    Sh77xxWdt*                      wdt_      = nullptr;
    bool                            detected_ = false;
    bool                            exited_   = false;
};

}

}
