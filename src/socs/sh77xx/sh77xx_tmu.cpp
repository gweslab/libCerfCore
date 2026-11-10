#include "sh77xx_tmu.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kOffTstr         = 0x02u;
constexpr uint32_t kOffChannels     = 0x04u;
constexpr uint32_t kChannelStride   = 0x0Cu;
constexpr uint32_t kOffTcpr2        = 0x28u;
constexpr uint32_t kChannelConstant = 0x00u;
constexpr uint32_t kChannelCount    = 0x04u;
constexpr uint32_t kChannelControl  = 0x08u;

constexpr uint8_t kTstrReserved = 0xF8u;

constexpr uint16_t kTcrTpsc     = 0x0007u;
constexpr uint16_t kTcrUnie     = 0x0020u;
constexpr uint16_t kTcrIcpe     = 0x00C0u;
constexpr uint16_t kTcrUnf      = 0x0100u;
constexpr uint16_t kTcrStored   = 0x003Fu;
constexpr uint16_t kTcrReserved = 0xFEC0u;
constexpr uint16_t kTcr2Stored  = 0x00FFu;
constexpr uint16_t kTcr2Reserved = 0xFC00u;

constexpr uint16_t kTpscRtcclk    = 4u;
constexpr uint16_t kTpscPclkLast  = 3u;
constexpr uint32_t kPclkTapShift  = 2u;
constexpr uint32_t kChannelInput2 = 2u;

constexpr uint8_t kStbcrMstp2 = 0x04u;

constexpr uint32_t kPowerOnCount = 0xFFFFFFFFu;

constexpr Sh77xxIntcSource kUnderflowSource[3] = {Sh77xxIntcSource::kTuni0, Sh77xxIntcSource::kTuni1,
                                                  Sh77xxIntcSource::kTuni2};

constexpr const char* kChannelPrefix[3] = {"tmu0_", "tmu1_", "tmu2_"};

uint32_t PclkShift(uint16_t control) { return kPclkTapShift + 2u * (control & kTcrTpsc); }

}

void Sh77xxTmu::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    intc_  = &emu_.Get<Sh77xxIntc>();
    stbcr_ = &emu_.Get<Sh77xxStbcr>();
    rtc_   = &emu_.Get<Sh77xxRtc>();
    for (Channel& ch : ch_) {
        Channel* channel = &ch;
        ch.event         = clock_->Add([this, channel] {
            Sync(*channel);
        });
    }
    pclk_.Attach([this] {
        for (Channel& ch : ch_) {
            if (!Rtcclk(ch)) Arm(ch);
        }
    });
    emu_.Get<Sh77xxStandbyEntry>().RegisterListener([this] { StopPeripheralClockChannels(); });
    stbcr_->RegisterChangeListener([this](uint8_t from, uint8_t to) { OnStandbyControlChange(from, to); });
    rtc_->RegisterRtcclkListener([this] {
        for (Channel& ch : ch_) {
            if (Rtcclk(ch)) Arm(ch);
        }
    });
    GuestDeepSleep& sleep = emu_.Get<GuestDeepSleep>();
    sleep.RegisterParkClock([this] { SyncRtcclkChannels(); });
    sleep.RegisterParkWakeDue([this] { return ParkWakeDueNs(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { PowerOnReset(); });
    PowerOnReset();
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Sh77xxTmu::Index(const Channel& ch) const { return static_cast<uint32_t>(&ch - ch_); }

bool Sh77xxTmu::Rtcclk(const Channel& ch) const { return (ch.control & kTcrTpsc) == kTpscRtcclk; }

uint64_t Sh77xxTmu::Ticks(const Channel& ch) {
    if (Rtcclk(ch)) return rtc_->RtcclkTicks();
    return pclk_.Ticks() >> PclkShift(ch.control);
}

void Sh77xxTmu::Arm(Channel& ch) {
    if (!ch.counter.Running() || (ch.control & kTcrUnie) == 0u || ch.counter.Flagged()) {
        clock_->Disarm(ch.event);
        return;
    }
    if (Rtcclk(ch)) {
        rtc_->ArmAtRtcclk(ch.event, ch.counter.NextFlag());
        return;
    }
    clock_->Arm(ch.event, pclk_.CycleOfTick(ch.counter.NextFlag() << PclkShift(ch.control)));
}

void Sh77xxTmu::Drive(Channel& ch) {
    intc_->SetSourceRequest(kUnderflowSource[Index(ch)], ch.counter.Flagged() && (ch.control & kTcrUnie) != 0u);
}

void Sh77xxTmu::Sync(Channel& ch) {
    ch.counter.Advance(Ticks(ch));
    Drive(ch);
    Arm(ch);
}

void Sh77xxTmu::RequireClock(const char* access, uint32_t addr, uint32_t value) const {
    if (!stbcr_->ModuleStopped(kStbcrMstp2)) return;
    emu_.Get<Fatal>().Die("Sh77xxTmu: %s of 0x%08X at 0x%08X while STBCR MSTP2 stops the TMU clock", access, value,
                          addr);
}

Sh77xxTmu::Field Sh77xxTmu::Locate(uint32_t addr, Channel*& ch) {
    const uint32_t off = addr - MmioBase();
    if (off < kOffChannels || off >= kOffTcpr2) return Field::kNone;
    const uint32_t rel = off - kOffChannels;
    ch                 = &ch_[rel / kChannelStride];
    switch (rel % kChannelStride) {
        case kChannelConstant: return Field::kConstant;
        case kChannelCount: return Field::kCount;
        case kChannelControl: return Field::kControl;
        default: return Field::kNone;
    }
}

uint8_t Sh77xxTmu::ReadTstr() const {
    uint8_t value = 0u;
    for (const Channel& ch : ch_) {
        if (ch.counter.Running()) value |= static_cast<uint8_t>(1u << Index(ch));
    }
    return value;
}

void Sh77xxTmu::WriteTstr(uint8_t value) {
    if ((value & kTstrReserved) != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxTmu: TSTR write 0x%02X sets reserved bits", value);
    }
    for (Channel& ch : ch_) {
        const bool start = (value & (1u << Index(ch))) != 0u;
        if (start == ch.counter.Running()) continue;
        if (start) {
            ch.counter.Start(Ticks(ch));
        } else {
            ch.counter.Stop(Ticks(ch));
        }
        Drive(ch);
        Arm(ch);
    }
}

uint16_t Sh77xxTmu::ReadControl(Channel& ch) {
    Sync(ch);
    return static_cast<uint16_t>(ch.control | (ch.counter.Flagged() ? kTcrUnf : 0u));
}

void Sh77xxTmu::WriteControl(Channel& ch, uint16_t value) {
    const bool     input2   = Index(ch) == kChannelInput2;
    const uint16_t reserved = input2 ? kTcr2Reserved : kTcrReserved;
    if ((value & reserved) != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxTmu: TCR%u write 0x%04X sets reserved bits", Index(ch), value);
    }
    if ((value & kTcrIcpe) != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxTmu: TCR%u write 0x%04X enables input capture", Index(ch), value);
    }
    const uint16_t tpsc = value & kTcrTpsc;
    if (tpsc > kTpscPclkLast && tpsc != kTpscRtcclk) {
        emu_.Get<Fatal>().Die("Sh77xxTmu: TCR%u write 0x%04X selects TPSC %u", Index(ch), value, tpsc);
    }
    Sync(ch);
    const uint64_t from = Ticks(ch);
    ch.control          = static_cast<uint16_t>(value & (input2 ? kTcr2Stored : kTcrStored));
    ch.counter.Move(from, Ticks(ch));
    if ((value & kTcrUnf) == 0u) ch.counter.ClearFlag();
    Drive(ch);
    Arm(ch);
}

uint8_t Sh77xxTmu::ReadByte(uint32_t addr) {
    RequireClock("ReadByte", addr, 0u);
    if (addr - MmioBase() != kOffTstr) HaltUnsupportedAccess("ReadByte", addr, 0u);
    return ReadTstr();
}

void Sh77xxTmu::WriteByte(uint32_t addr, uint8_t value) {
    RequireClock("WriteByte", addr, value);
    if (addr - MmioBase() != kOffTstr) HaltUnsupportedAccess("WriteByte", addr, value);
    WriteTstr(value);
}

uint16_t Sh77xxTmu::ReadHalf(uint32_t addr) {
    RequireClock("ReadHalf", addr, 0u);
    Channel* ch = nullptr;
    if (Locate(addr, ch) != Field::kControl) HaltUnsupportedAccess("ReadHalf", addr, 0u);
    return ReadControl(*ch);
}

void Sh77xxTmu::WriteHalf(uint32_t addr, uint16_t value) {
    RequireClock("WriteHalf", addr, value);
    Channel* ch = nullptr;
    if (Locate(addr, ch) != Field::kControl) HaltUnsupportedAccess("WriteHalf", addr, value);
    WriteControl(*ch, value);
}

uint32_t Sh77xxTmu::ReadWord(uint32_t addr) {
    RequireClock("ReadWord", addr, 0u);
    Channel*    ch    = nullptr;
    const Field field = Locate(addr, ch);
    if (field == Field::kConstant) return ch->counter.Constant();
    if (field != Field::kCount) HaltUnsupportedAccess("ReadWord", addr, 0u);
    const uint32_t count = ch->counter.Count(Ticks(*ch));
    Drive(*ch);
    Arm(*ch);
    return count;
}

void Sh77xxTmu::WriteWord(uint32_t addr, uint32_t value) {
    RequireClock("WriteWord", addr, value);
    Channel*    ch    = nullptr;
    const Field field = Locate(addr, ch);
    if (field == Field::kConstant) {
        ch->counter.WriteConstant(Ticks(*ch), value);
    } else if (field == Field::kCount) {
        ch->counter.WriteCount(Ticks(*ch), value);
    } else {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    Drive(*ch);
    Arm(*ch);
}

void Sh77xxTmu::PowerOnReset() {
    for (Channel& ch : ch_) {
        ch.counter.PowerOnReset(kPowerOnCount);
        ch.control = 0u;
        clock_->Disarm(ch.event);
        Drive(ch);
    }
}

void Sh77xxTmu::StopPeripheralClockChannels() {
    for (Channel& ch : ch_) {
        if (Rtcclk(ch) || !ch.counter.Running()) continue;
        ch.counter.Stop(Ticks(ch));
        Drive(ch);
        Arm(ch);
    }
}

void Sh77xxTmu::SyncRtcclkChannels() {
    for (Channel& ch : ch_) {
        if (Rtcclk(ch)) Sync(ch);
    }
}

int64_t Sh77xxTmu::ParkWakeDueNs() {
    SyncRtcclkChannels();
    int64_t due = GuestDeepSleep::kNoParkWake;
    for (Channel& ch : ch_) {
        if (!Rtcclk(ch) || !ch.counter.Running() || (ch.control & kTcrUnie) == 0u || ch.counter.Flagged()) continue;
        if (!intc_->WakesStandby(kUnderflowSource[Index(ch)])) continue;
        const int64_t at = rtc_->RtcclkSleptNsAtTick(ch.counter.NextFlag());
        if (at < due) due = at;
    }
    return due;
}

void Sh77xxTmu::OnStandbyControlChange(uint8_t from, uint8_t to) {
    if ((from & kStbcrMstp2) != 0u || (to & kStbcrMstp2) == 0u) return;
    for (const Channel& ch : ch_) {
        if (Rtcclk(ch) && ch.counter.Running()) {
            emu_.Get<Fatal>().Die("Sh77xxTmu: STBCR MSTP2 set while channel %u counts RTCCLK", Index(ch));
        }
    }
    StopPeripheralClockChannels();
}

void Sh77xxTmu::SaveState(StateWriter& w) {
    for (Channel& ch : ch_) {
        ch.counter.Save(w, kChannelPrefix[Index(ch)], Ticks(ch));
        w.Write((std::string(kChannelPrefix[Index(ch)]) + "control").c_str(), ch.control);
    }
    pclk_.Save(w);
}

void Sh77xxTmu::RestoreState(StateReader& r) {
    for (Channel& ch : ch_) {
        ch.counter.Restore(r, kChannelPrefix[Index(ch)]);
        r.Read((std::string(kChannelPrefix[Index(ch)]) + "control").c_str(), ch.control);
    }
    pclk_.Restore(r);
}

void Sh77xxTmu::PostRestore() {
    pclk_.PostRestore();
    for (Channel& ch : ch_) {
        ch.counter.Rebase(Ticks(ch));
        Drive(ch);
        Arm(ch);
    }
}

}
