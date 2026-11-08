#include "s3c2410_watchdog.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../irq_controller.h"
#include "s3c2410_clocks.h"
#include "s3c2410_id.h"

namespace cerfcore {

REGISTER_SERVICE(S3C2410Watchdog);

namespace {

constexpr uint32_t kOffWtcon = 0x00u;
constexpr uint32_t kOffWtdat = 0x04u;
constexpr uint32_t kOffWtcnt = 0x08u;

constexpr uint32_t kWtconReset = 0x8021u;
constexpr uint32_t kWtdatReset = 0x8000u;
constexpr uint32_t kWtcntReset = 0x8000u;

constexpr uint32_t kWtconEnable    = 1u << 5;
constexpr uint32_t kWtconIntEnable = 1u << 2;
constexpr uint32_t kWtconRstEnable = 1u << 0;
constexpr uint32_t kWtconReserved  = 0xFFFF00C2u;

constexpr int kIrqWdt = 9;

uint32_t DivisionFactor(uint32_t wtcon) { return 16u << ((wtcon >> 3) & 0x3u); }

}

bool S3C2410Watchdog::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::S3c2410;
}

void S3C2410Watchdog::OnReady() {
    clock_  = &emu_.Get<GuestCycleClock>();
    clocks_ = &emu_.Get<S3C2410Clocks>();
    event_  = clock_->Add([this] { OnTimeout(); });
    ResetRegisters();
    clocks_->RegisterRateListener([this] { OnRateChange(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetRegisters(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

bool S3C2410Watchdog::Running() const { return (wtcon_ & kWtconEnable) != 0u; }

GuestCycleClock::Rate S3C2410Watchdog::CoreRate() const {
    return GuestCycleClock::Rate{clocks_->CoreClockHz(), 1u};
}

GuestCycleClock::Rate S3C2410Watchdog::TickRate(uint32_t wtcon) const {
    return GuestCycleClock::Rate{clocks_->PclkHz(), (((wtcon >> 8) & 0xFFu) + 1u) * DivisionFactor(wtcon)};
}

uint32_t S3C2410Watchdog::CountAt(uint64_t now) const {
    if (!Running()) return count_;
    const uint64_t since = ticks_.TicksAt(now) - anchor_tick_;
    if (since < count_) return static_cast<uint32_t>(count_ - since);
    if (wtdat_ == 0u) return 0u;
    return wtdat_ - static_cast<uint32_t>((since - count_) % wtdat_);
}

void S3C2410Watchdog::Anchor(uint64_t now, uint32_t count) {
    count_       = count;
    anchor_tick_ = ticks_.TicksAt(now);
}

void S3C2410Watchdog::Restart(uint64_t now, uint32_t count) {
    if (!ticks_.SetRate(CoreRate(), TickRate(wtcon_))) {
        emu_.Get<Fatal>().Die("S3C2410Watchdog: PCLK %llu Hz with WTCON 0x%04X on a %llu Hz core "
                              "overflows the 64-bit scale",
                              static_cast<unsigned long long>(clocks_->PclkHz()), wtcon_,
                              static_cast<unsigned long long>(clocks_->CoreClockHz()));
    }
    ticks_.Start(now);
    Anchor(now, count);
    Arm();
}

void S3C2410Watchdog::Arm() {
    if (!Running() || (wtcon_ & (kWtconIntEnable | kWtconRstEnable)) == 0u) {
        clock_->Disarm(event_);
        return;
    }
    clock_->Arm(event_, ticks_.CycleOfTick(anchor_tick_ + count_));
}

void S3C2410Watchdog::ResetRegisters() {
    wtcon_ = kWtconReset;
    wtdat_ = kWtdatReset;
    Restart(clock_->Cycles(), kWtcntReset);
}

void S3C2410Watchdog::WriteControl(uint32_t value, uint64_t now) {
    if ((value & kWtconReserved) != 0u) {
        emu_.Get<Fatal>().Die("S3C2410Watchdog: WTCON write 0x%08X sets reserved bits", value);
    }
    const uint32_t count = CountAt(now);
    if (!Running() && (value & kWtconEnable) != 0u && count == 0u) {
        emu_.Get<Fatal>().Die("S3C2410Watchdog: WTCON write 0x%08X enables the watchdog with "
                              "WTCNT 0", value);
    }
    wtcon_ = value;
    Restart(now, count);
}

void S3C2410Watchdog::WriteRegister(uint32_t off, uint32_t value) {
    const uint64_t now = clock_->Cycles();
    switch (off) {
        case kOffWtcon: WriteControl(value, now); return;
        case kOffWtdat: wtdat_ = value & 0xFFFFu; return;
        case kOffWtcnt:
            Anchor(now, value & 0xFFFFu);
            Arm();
            return;
    }
    HaltUnsupportedAccess("WriteWord", MmioBase() + off, value);
}

uint32_t S3C2410Watchdog::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        case kOffWtcon: return wtcon_;
        case kOffWtdat: return wtdat_;
        case kOffWtcnt: return CountAt(clock_->Cycles());
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void S3C2410Watchdog::WriteWord(uint32_t addr, uint32_t value) {
    LOG(SocWdt, "write +0x%02X = 0x%08X\n", addr - MmioBase(), value);
    WriteRegister(addr - MmioBase(), value);
}

void S3C2410Watchdog::OnTimeout() {
    LOG(SocWdt, "time-out: WTCON 0x%04X WTDAT 0x%04X\n", wtcon_, wtdat_);
    if ((wtcon_ & kWtconRstEnable) != 0u) {
        emu_.Get<GuestCpuReset>().WatchdogReset();
        return;
    }
    if (wtdat_ == 0u) {
        emu_.Get<Fatal>().Die("S3C2410Watchdog: interval time-out reloads WTDAT 0 (WTCON 0x%04X)",
                              wtcon_);
    }
    anchor_tick_ += count_;
    count_ = wtdat_;
    Arm();
    if ((wtcon_ & kWtconIntEnable) != 0u) emu_.Get<IrqController>().AssertIrq(kIrqWdt);
}

void S3C2410Watchdog::OnRateChange() {
    const uint64_t now = clock_->Cycles();
    Restart(now, CountAt(now));
}

void S3C2410Watchdog::SaveState(StateWriter& w) {
    w.Write("wtcon", wtcon_);
    w.Write("wtdat", wtdat_);
    w.Write("wtcnt", CountAt(clock_->Cycles()));
}

void S3C2410Watchdog::RestoreState(StateReader& r) {
    r.Read("wtcon", wtcon_);
    r.Read("wtdat", wtdat_);
    r.Read("wtcnt", count_);
}

void S3C2410Watchdog::PostRestore() {
    Restart(clock_->Cycles(), count_);
}

}
