#include "sh77xx_rtc.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace cerfcore {

namespace {

using Cal = Sh77xxRtcCalendar;

constexpr uint64_t kCrystalHz      = 32768u;
constexpr uint64_t kRtcclkDivider  = 2u;
constexpr uint64_t kTicksPerCount  = 256u;
constexpr uint64_t kTicksPerSecond = kCrystalHz;
constexpr uint64_t kDividerPeriod  = 2u * kTicksPerSecond;

constexpr uint64_t kPeriodTicks[8] = {0u, 128u, 512u, 2048u, 8192u, 16384u, 32768u, 65536u};

constexpr uint32_t kOffR64cnt  = 0x00u;
constexpr uint32_t kOffCounter = 0x02u;
constexpr uint32_t kOffAlarm   = 0x10u;
constexpr uint32_t kOffRcr1    = 0x1Cu;
constexpr uint32_t kOffRcr2    = 0x1Eu;

constexpr uint8_t kRcr1Cf       = 0x80u;
constexpr uint8_t kRcr1Cie      = 0x10u;
constexpr uint8_t kRcr1Aie      = 0x08u;
constexpr uint8_t kRcr1Af       = 0x01u;
constexpr uint8_t kRcr1Reserved = 0x66u;

constexpr uint8_t kRcr2Pef      = 0x80u;
constexpr uint8_t kRcr2PesShift = 4u;
constexpr uint8_t kRcr2PesMask  = 0x07u;
constexpr uint8_t kRcr2Rtcen    = 0x08u;
constexpr uint8_t kRcr2Adj      = 0x04u;
constexpr uint8_t kRcr2Reset    = 0x02u;
constexpr uint8_t kRcr2Start    = 0x01u;

constexpr uint8_t kStbcrMstp1 = 0x02u;

uint8_t Flag(bool set, uint8_t bit) { return set ? bit : static_cast<uint8_t>(0u); }

}

void Sh77xxRtc::OnReady() {
    intc_  = &emu_.Get<Sh77xxIntc>();
    stbcr_ = &emu_.Get<Sh77xxStbcr>();
    clock_ = &emu_.Get<GuestCycleClock>();
    osc_.Attach(kCrystalHz, 1u);
    seen_ = osc_.Now();
    cal_.SeedFromHost();
    event_ = clock_->Add([this] {
        Advance();
        Settle();
    });
    clock_->RegisterRateListener([this] {
        Advance();
        osc_.Rescale();
        Schedule();
        NotifyRtcclk();
    });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) { OnReset(kind); });
    GuestDeepSleep& sleep = emu_.Get<GuestDeepSleep>();
    sleep.RegisterParkClock([this] {
        Advance();
        Settle();
    });
    sleep.RegisterParkWakeDue([this] { return ParkWakeDueNs(); });
    PowerOnReset();
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Sh77xxRtc::RequireClock(const char* access, uint32_t addr, uint8_t value) const {
    if (!stbcr_->ModuleStopped(kStbcrMstp1)) return;
    emu_.Get<Fatal>().Die("Sh77xxRtc: %s of 0x%02X at 0x%08X while STBCR MSTP1 stops the RTC clock", access, value,
                          addr);
}

void Sh77xxRtc::RequireCountable(const char* trigger) const {
    if (cal_.Value(Cal::kMonth) == 0u) {
        emu_.Get<Fatal>().Die("Sh77xxRtc: %s counts from the month counter at 00, which is not modelled", trigger);
    }
    if (!cal_.DateFitsMonth()) {
        emu_.Get<Fatal>().Die("Sh77xxRtc: %s counts from date %u beyond the length of month %u in year %02u", trigger,
                              cal_.Value(Cal::kDate), cal_.Value(Cal::kMonth), cal_.Value(Cal::kYear));
    }
}

uint64_t Sh77xxRtc::PeriodTicks() const { return kPeriodTicks[pes_]; }

void Sh77xxRtc::Advance() {
    const uint64_t now = osc_.Now();
    const uint64_t from = phase_;
    const uint64_t to   = from + (now - seen_);
    seen_               = now;
    if (to == from) return;
    if (to / kTicksPerCount != from / kTicksPerCount) cf_ = true;
    if (pes_ != 0u && to / PeriodTicks() != from / PeriodTicks()) pef_ = true;
    const uint64_t secs = to / kTicksPerSecond - from / kTicksPerSecond;
    phase_              = static_cast<uint32_t>(to % kDividerPeriod);
    if (secs != 0u && start_) CountSeconds(secs);
}

void Sh77xxRtc::CountSeconds(uint64_t secs) {
    if (!Cal::AlarmEnabled(alarm_)) {
        cal_.Advance(secs);
        return;
    }
    for (uint64_t second = 0u; second < secs; ++second) {
        cal_.Advance(1u);
        Compare();
    }
}

void Sh77xxRtc::Compare() {
    const bool match = cal_.Matches(alarm_);
    if (match && !match_) {
        af_          = true;
        alarm_stale_ = true;
    }
    match_ = match;
}

void Sh77xxRtc::Settle() {
    DriveLines();
    Schedule();
}

uint64_t Sh77xxRtc::NextEventTick(bool waking_only) {
    uint64_t   next     = std::numeric_limits<uint64_t>::max();
    const auto consider = [&](uint64_t period) { next = std::min(next, seen_ + period - phase_ % period); };
    const auto counts   = [&](Sh77xxIntcSource source) { return !waking_only || intc_->WakesStandby(source); };
    if (cie_ && !cf_ && counts(Sh77xxIntcSource::kCui)) consider(kTicksPerCount);
    if (pes_ != 0u && !pef_ && counts(Sh77xxIntcSource::kPri)) consider(PeriodTicks());
    if (aie_ && !af_ && start_ && Cal::AlarmEnabled(alarm_) && counts(Sh77xxIntcSource::kAti)) {
        if (alarm_stale_) RecomputeAlarmDue();
        if (alarm_reachable_) next = std::min(next, alarm_due_);
    }
    return next;
}

void Sh77xxRtc::Schedule() {
    const uint64_t next = NextEventTick(false);
    if (next == std::numeric_limits<uint64_t>::max()) {
        clock_->Disarm(event_);
        return;
    }
    osc_.ArmAt(event_, next);
}

int64_t Sh77xxRtc::ParkWakeDueNs() {
    Advance();
    Settle();
    const uint64_t next = NextEventTick(true);
    if (!rtcen_ || next == std::numeric_limits<uint64_t>::max()) return GuestDeepSleep::kNoParkWake;
    return osc_.SleptNsAtTick(next);
}

void Sh77xxRtc::RecomputeAlarmDue() {
    alarm_stale_        = false;
    const uint64_t secs = cal_.SecondsToOnset(alarm_, match_);
    alarm_reachable_    = secs != 0u;
    if (alarm_reachable_) {
        alarm_due_ = seen_ + (kTicksPerSecond - phase_ % kTicksPerSecond) + (secs - 1u) * kTicksPerSecond;
    }
}

void Sh77xxRtc::DriveLines() {
    intc_->SetSourceRequest(Sh77xxIntcSource::kAti, af_ && aie_);
    intc_->SetSourceRequest(Sh77xxIntcSource::kPri, pef_);
    intc_->SetSourceRequest(Sh77xxIntcSource::kCui, cf_ && cie_);
}

void Sh77xxRtc::SetCounting(bool counting) {
    rtcen_ = counting;
    osc_.SetCounting(counting);
    NotifyRtcclk();
}

uint64_t Sh77xxRtc::RtcclkTicks() { return osc_.Now() / kRtcclkDivider; }

void Sh77xxRtc::ArmAtRtcclk(GuestCycleClock::Event* event, uint64_t tick) {
    osc_.ArmAt(event, tick * kRtcclkDivider);
}

int64_t Sh77xxRtc::RtcclkSleptNsAtTick(uint64_t tick) {
    if (!rtcen_) return GuestDeepSleep::kNoParkWake;
    return osc_.SleptNsAtTick(tick * kRtcclkDivider);
}

void Sh77xxRtc::RegisterRtcclkListener(std::function<void()> listener) {
    rtcclk_listeners_.push_back(std::move(listener));
}

void Sh77xxRtc::NotifyRtcclk() {
    for (const std::function<void()>& listener : rtcclk_listeners_) listener();
}

void Sh77xxRtc::OnReset(ResetLineKind kind) {
    Advance();
    if (kind == ResetLineKind::Rtc) {
        PowerOnReset();
    } else {
        ManualReset();
    }
    Settle();
}

void Sh77xxRtc::PowerOnReset() {
    alarm_stale_ = true;
    cf_          = false;
    cie_         = false;
    aie_         = false;
    af_          = false;
    pef_         = false;
    pes_         = 0u;
    if (!rtcen_) SetCounting(true);
    if (!start_) RequireCountable("power-on reset");
    start_ = true;
    for (uint8_t& field : alarm_) field &= static_cast<uint8_t>(~Cal::kAlarmEnable);
    match_ = false;
}

void Sh77xxRtc::ManualReset() {
    cie_ = false;
    aie_ = false;
    af_  = false;
    pef_ = false;
    pes_ = 0u;
}

uint8_t Sh77xxRtc::ReadRcr1() const {
    return static_cast<uint8_t>(Flag(cf_, kRcr1Cf) | Flag(cie_, kRcr1Cie) | Flag(aie_, kRcr1Aie) |
                                Flag(af_, kRcr1Af));
}

uint8_t Sh77xxRtc::ReadRcr2() const {
    return static_cast<uint8_t>(Flag(pef_, kRcr2Pef) | (pes_ << kRcr2PesShift) | Flag(rtcen_, kRcr2Rtcen) |
                                Flag(start_, kRcr2Start));
}

void Sh77xxRtc::WriteRcr1(uint8_t value) {
    if ((value & kRcr1Reserved) != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxRtc: RCR1 write 0x%02X sets reserved bits 0x%02X", value, value & kRcr1Reserved);
    }
    cf_  = (value & kRcr1Cf) != 0u;
    cie_ = (value & kRcr1Cie) != 0u;
    aie_ = (value & kRcr1Aie) != 0u;
    if ((value & kRcr1Af) == 0u) af_ = false;
}

void Sh77xxRtc::WriteRcr2(uint8_t value) {
    pef_ = (value & kRcr2Pef) != 0u;
    pes_ = static_cast<uint8_t>((value >> kRcr2PesShift) & kRcr2PesMask);
    const bool rtcen = (value & kRcr2Rtcen) != 0u;
    if (rtcen != rtcen_) SetCounting(rtcen);
    if ((value & kRcr2Adj) != 0u) {
        if (cal_.CrossesDay(cal_.RoundUpSeconds())) RequireCountable("RCR2 ADJ");
        cal_.RoundToMinute();
        phase_       = 0u;
        alarm_stale_ = true;
        Compare();
    }
    if ((value & kRcr2Reset) != 0u) {
        phase_       = 0u;
        alarm_stale_ = true;
    }
    const bool start = (value & kRcr2Start) != 0u;
    if (start != start_) alarm_stale_ = true;
    if (start && !start_) {
        RequireCountable("RCR2 START");
        LOG(SocRtc, "[RTC] Sh77xxRtc: counting from %02u-%02u-%02u %02u:%02u:%02u weekday %u\n",
            cal_.Value(Cal::kYear), cal_.Value(Cal::kMonth), cal_.Value(Cal::kDate), cal_.Value(Cal::kHour),
            cal_.Value(Cal::kMinute), cal_.Value(Cal::kSecond), cal_.Value(Cal::kWeekday));
    }
    start_ = start;
}

void Sh77xxRtc::WriteCounter(Cal::Counter counter, uint8_t value) {
    if (start_) {
        emu_.Get<Fatal>().Die("Sh77xxRtc: counter %u write 0x%02X while RCR2 START = 1", counter, value);
    }
    if (!cal_.Write(counter, value)) {
        emu_.Get<Fatal>().Die("Sh77xxRtc: counter %u write 0x%02X outside its range", counter, value);
    }
    alarm_stale_ = true;
    Compare();
}

void Sh77xxRtc::WriteAlarm(uint32_t field, uint8_t value) {
    if (!Cal::AlarmFieldValid(field, value)) {
        emu_.Get<Fatal>().Die("Sh77xxRtc: alarm %u write 0x%02X enables a value outside its range", field, value);
    }
    alarm_[field] = static_cast<uint8_t>(value & (Cal::kAlarmEnable | Cal::FieldMask(field)));
    alarm_stale_  = true;
    Compare();
}

uint8_t Sh77xxRtc::ReadByte(uint32_t addr) {
    RequireClock("read", addr, 0u);
    Advance();
    const uint32_t off   = addr - MmioBase();
    uint8_t        value = 0u;
    if (off == kOffR64cnt) {
        value = static_cast<uint8_t>((phase_ % kTicksPerSecond) / kTicksPerCount);
    } else if (off >= kOffCounter && off < kOffAlarm && (off & 1u) == 0u) {
        value = cal_.Register(static_cast<Cal::Counter>((off - kOffCounter) / 2u));
    } else if (off >= kOffAlarm && off < kOffRcr1 && (off & 1u) == 0u) {
        value = alarm_[(off - kOffAlarm) / 2u];
    } else if (off == kOffRcr1) {
        value = ReadRcr1();
    } else if (off == kOffRcr2) {
        value = ReadRcr2();
    } else {
        HaltUnsupportedAccess("ReadByte", addr, 0u);
    }
    Settle();
    return value;
}

void Sh77xxRtc::WriteByte(uint32_t addr, uint8_t value) {
    RequireClock("write", addr, value);
    Advance();
    const uint32_t off = addr - MmioBase();
    if (off >= kOffCounter && off < kOffAlarm && (off & 1u) == 0u) {
        WriteCounter(static_cast<Cal::Counter>((off - kOffCounter) / 2u), value);
    } else if (off >= kOffAlarm && off < kOffRcr1 && (off & 1u) == 0u) {
        WriteAlarm((off - kOffAlarm) / 2u, value);
    } else if (off == kOffRcr1) {
        WriteRcr1(value);
    } else if (off == kOffRcr2) {
        WriteRcr2(value);
    } else {
        HaltUnsupportedAccess("WriteByte", addr, value);
    }
    Settle();
}

void Sh77xxRtc::SaveState(StateWriter& w) {
    Advance();
    osc_.Save(w);
    cal_.Save(w);
    w.WriteBytes("alarm", alarm_.data(), sizeof(alarm_));
    w.Write("phase", phase_);
    w.Write("pes", pes_);
    w.Write("cf", cf_);
    w.Write("cie", cie_);
    w.Write("aie", aie_);
    w.Write("af", af_);
    w.Write("pef", pef_);
    w.Write("rtcen", rtcen_);
    w.Write("start", start_);
    w.Write("match", match_);
}

void Sh77xxRtc::RestoreState(StateReader& r) {
    osc_.Restore(r);
    cal_.Restore(r);
    r.ReadBytes("alarm", alarm_.data(), sizeof(alarm_));
    r.Read("phase", phase_);
    r.Read("pes", pes_);
    r.Read("cf", cf_);
    r.Read("cie", cie_);
    r.Read("aie", aie_);
    r.Read("af", af_);
    r.Read("pef", pef_);
    r.Read("rtcen", rtcen_);
    r.Read("start", start_);
    r.Read("match", match_);
    seen_        = osc_.Now();
    alarm_stale_ = true;
}

void Sh77xxRtc::PostRestore() {
    DriveLines();
    Schedule();
}

}
