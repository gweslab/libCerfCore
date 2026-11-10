#include "hd64461_timer.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "../../cpu/sh/sh_board_clock.h"
#include "../peripheral_dispatcher.h"
#include "hd64461_intc.h"
#include "hd64461_system_control.h"

#include <string>

namespace cerfcore {

REGISTER_SERVICE(Hd64461Timer);

namespace {

constexpr uint32_t kOffConstant1 = 0x0u;
constexpr uint32_t kOffConstant0 = 0x2u;
constexpr uint32_t kOffCount1    = 0x4u;
constexpr uint32_t kOffCount0    = 0x6u;
constexpr uint32_t kOffControl1  = 0x8u;
constexpr uint32_t kOffControl0  = 0xAu;
constexpr uint32_t kOffRequests  = 0xCu;
constexpr uint32_t kOffMasks     = 0xEu;

constexpr uint16_t kTcrStart    = 0x0001u;
constexpr uint16_t kTcrPrescale = 0x0006u;
constexpr uint16_t kTcrOutput   = 0x0008u;
constexpr uint16_t kTcrReserved = 0xFFF0u;
constexpr uint16_t kLowLane     = 0x00FFu;

constexpr uint16_t kChannelReserved = 0xFFFCu;

constexpr uint32_t kPowerOnConstant = 0xFFFFu;

constexpr uint32_t kPrescaleShift[4] = {4u, 3u, 2u, 0u};

constexpr Hd64461IntcSource kRequestSource[2] = {Hd64461IntcSource::kTmu0, Hd64461IntcSource::kTmu1};

constexpr const char* kChannelPrefix[2] = {"timer0_", "timer1_"};

uint32_t PrescaleShift(uint16_t control) { return kPrescaleShift[(control & kTcrPrescale) >> 1]; }

}

bool Hd64461Timer::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
}

void Hd64461Timer::OnReady() {
    clock_  = &emu_.Get<GuestCycleClock>();
    intc_   = &emu_.Get<Hd64461Intc>();
    system_ = &emu_.Get<Hd64461SystemControl>();
    ckio_.Attach(emu_.Get<ShBoardClock>().CkioHz(), 1u);
    for (Channel& ch : ch_) {
        Channel* channel = &ch;
        ch.event         = clock_->Add([this, channel] { Sync(*channel); });
    }
    clock_->RegisterRateListener([this] { Retime(); });
    GuestCpuReset& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind kind) {
        if (kind == ResetLineKind::Rtc) PowerOnReset();
    });
    reset.RegisterResetReleaseListener([this] { OnModuleClockChanged(); });
    PowerOnReset();
    OnModuleClockChanged();
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Hd64461Timer::Index(const Channel& ch) const { return static_cast<uint32_t>(&ch - ch_); }

uint64_t Hd64461Timer::Ticks(const Channel& ch) { return ckio_.Now() >> PrescaleShift(ch.control); }

bool Hd64461Timer::Counting(const Channel& ch) const { return (ch.control & kTcrStart) != 0u && ch.clocked; }

void Hd64461Timer::Arm(Channel& ch) {
    if (!Counting(ch) || ch.masked || ch.counter.Flagged()) {
        clock_->Disarm(ch.event);
        return;
    }
    ckio_.ArmAt(ch.event, ch.counter.NextFlag() << PrescaleShift(ch.control));
}

void Hd64461Timer::Drive(Channel& ch) {
    intc_->SetSourceRequest(kRequestSource[Index(ch)], ch.counter.Flagged() && !ch.masked);
}

void Hd64461Timer::Sync(Channel& ch) {
    ch.counter.Advance(Ticks(ch));
    Drive(ch);
    Arm(ch);
}

void Hd64461Timer::RequireNoOutputPulse(const Channel& ch) const {
    if ((ch.control & kTcrOutput) == 0u || !Counting(ch)) return;
    emu_.Get<Fatal>().Die("Hd64461Timer: timer %u counts with TMO%u# enabled (TCR 0x%04X)", Index(ch), Index(ch),
                          ch.control);
}

uint16_t Hd64461Timer::ReadCount(Channel& ch) {
    if (!Counting(ch)) {
        emu_.Get<Fatal>().Die("Hd64461Timer: TRVR%u read while timer %u does not count (TCR 0x%04X, clock %u)",
                              Index(ch), Index(ch), ch.control, ch.clocked ? 1u : 0u);
    }
    const uint32_t count = ch.counter.Count(Ticks(ch));
    Drive(ch);
    Arm(ch);
    return static_cast<uint16_t>(count);
}

void Hd64461Timer::WriteControl(Channel& ch, uint16_t value) {
    if ((value & kTcrReserved) != 0u) {
        emu_.Get<Fatal>().Die("Hd64461Timer: TCR%u write 0x%04X sets reserved bits", Index(ch), value);
    }
    const bool was_started = (ch.control & kTcrStart) != 0u;
    const bool starts      = (value & kTcrStart) != 0u;
    if (was_started && starts && ((ch.control ^ value) & kTcrPrescale) != 0u) {
        emu_.Get<Fatal>().Die("Hd64461Timer: TCR%u write 0x%04X changes the prescaler while TCR is 0x%04X",
                              Index(ch), value, ch.control);
    }
    if (was_started && !starts && ch.clocked) ch.counter.Stop(Ticks(ch));
    ch.control = value;
    if (!was_started && starts) {
        ch.counter.WriteCount(Ticks(ch), ch.counter.Constant());
        if (ch.clocked) ch.counter.Start(Ticks(ch));
    }
    RequireNoOutputPulse(ch);
    Drive(ch);
    Arm(ch);
}

uint16_t Hd64461Timer::ReadRequests() {
    uint16_t value = 0u;
    for (Channel& ch : ch_) {
        Sync(ch);
        if (ch.counter.Flagged()) value |= static_cast<uint16_t>(1u << Index(ch));
    }
    return value;
}

void Hd64461Timer::WriteRequests(uint16_t value) {
    if ((value & kChannelReserved) != 0u) {
        emu_.Get<Fatal>().Die("Hd64461Timer: TIRR write 0x%04X sets reserved bits", value);
    }
    for (Channel& ch : ch_) {
        Sync(ch);
        const bool keep = (value & (1u << Index(ch))) != 0u;
        if (keep && !ch.counter.Flagged()) {
            emu_.Get<Fatal>().Die("Hd64461Timer: TIRR write 0x%04X sets TMU%uR while timer %u requests nothing", value,
                                  Index(ch), Index(ch));
        }
        if (!keep) ch.counter.ClearFlag();
        Drive(ch);
        Arm(ch);
    }
}

uint16_t Hd64461Timer::ReadMasks() const {
    uint16_t value = 0u;
    for (const Channel& ch : ch_) {
        if (ch.masked) value |= static_cast<uint16_t>(1u << Index(ch));
    }
    return value;
}

void Hd64461Timer::WriteMasks(uint16_t value) {
    if ((value & kChannelReserved) != 0u) {
        emu_.Get<Fatal>().Die("Hd64461Timer: TER write 0x%04X sets reserved bits", value);
    }
    for (Channel& ch : ch_) {
        Sync(ch);
        ch.masked = (value & (1u << Index(ch))) != 0u;
        Drive(ch);
        Arm(ch);
    }
}

uint16_t Hd64461Timer::ReadHalf(uint32_t addr) {
    switch (addr - MmioBase()) {
        case kOffConstant1: return static_cast<uint16_t>(ch_[1].counter.Constant());
        case kOffConstant0: return static_cast<uint16_t>(ch_[0].counter.Constant());
        case kOffCount1: return ReadCount(ch_[1]);
        case kOffCount0: return ReadCount(ch_[0]);
        case kOffControl1: return ch_[1].control;
        case kOffControl0: return ch_[0].control;
        case kOffRequests: return ReadRequests();
        case kOffMasks: return ReadMasks();
        default: HaltUnsupportedAccess("ReadHalf", addr, 0u);
    }
}

void Hd64461Timer::WriteHalf(uint32_t addr, uint16_t value) {
    switch (addr - MmioBase()) {
        case kOffConstant1:
        case kOffConstant0: {
            Channel& ch = ch_[addr - MmioBase() == kOffConstant1 ? 1u : 0u];
            if (value == 0u) {
                emu_.Get<Fatal>().Die("Hd64461Timer: TCVR%u write 0x0000", Index(ch));
            }
            if ((ch.control & kTcrStart) != 0u && !ch.clocked) {
                emu_.Get<Fatal>().Die("Hd64461Timer: TCVR%u write 0x%04X while TCR is 0x%04X and the timer %u clock "
                                      "is stopped",
                                      Index(ch), value, ch.control, Index(ch));
            }
            ch.counter.WriteConstant(Ticks(ch), value);
            Drive(ch);
            Arm(ch);
            return;
        }
        case kOffControl1: WriteControl(ch_[1], value); return;
        case kOffControl0: WriteControl(ch_[0], value); return;
        case kOffRequests: WriteRequests(value); return;
        case kOffMasks: WriteMasks(value); return;
        default: HaltUnsupportedAccess("WriteHalf", addr, value);
    }
}

void Hd64461Timer::WriteByte(uint32_t addr, uint8_t value) {
    if (addr - MmioBase() != kOffControl1) HaltUnsupportedAccess("WriteByte", addr, value);
    WriteControl(ch_[1], static_cast<uint16_t>((ch_[1].control & ~kLowLane) | value));
}

void Hd64461Timer::SetClocked(Channel& ch, bool clocked) {
    if (clocked == ch.clocked) return;
    if ((ch.control & kTcrStart) != 0u && !clocked) ch.counter.Stop(Ticks(ch));
    ch.clocked = clocked;
    if ((ch.control & kTcrStart) != 0u && clocked) ch.counter.Start(Ticks(ch));
    RequireNoOutputPulse(ch);
    Drive(ch);
    Arm(ch);
}

void Hd64461Timer::OnModuleClockChanged() {
    for (Channel& ch : ch_) SetClocked(ch, system_->TimerClockRunning(Index(ch)));
}

void Hd64461Timer::PowerOnReset() {
    for (Channel& ch : ch_) {
        ch.counter.PowerOnReset(kPowerOnConstant);
        ch.control = 0u;
        ch.masked  = false;
        Drive(ch);
        Arm(ch);
    }
}

void Hd64461Timer::Retime() {
    ckio_.Rescale();
    for (Channel& ch : ch_) Arm(ch);
}

void Hd64461Timer::SaveState(StateWriter& w) {
    ckio_.Save(w, "ckio_");
    for (Channel& ch : ch_) {
        const std::string prefix(kChannelPrefix[Index(ch)]);
        ch.counter.Save(w, prefix.c_str(), Ticks(ch));
        w.Write((prefix + "control").c_str(), ch.control);
        w.Write((prefix + "masked").c_str(), ch.masked);
    }
}

void Hd64461Timer::RestoreState(StateReader& r) {
    ckio_.Restore(r, "ckio_");
    for (Channel& ch : ch_) {
        const std::string prefix(kChannelPrefix[Index(ch)]);
        ch.counter.Restore(r, prefix.c_str());
        r.Read((prefix + "control").c_str(), ch.control);
        r.Read((prefix + "masked").c_str(), ch.masked);
    }
}

void Hd64461Timer::PostRestore() {
    for (Channel& ch : ch_) {
        ch.clocked = system_->TimerClockRunning(Index(ch));
        ch.counter.Rebase(Ticks(ch));
        Drive(ch);
        Arm(ch);
    }
}

}
