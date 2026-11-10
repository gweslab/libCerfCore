#include "sh77xx_cmt.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kOffCmstr = 0u;
constexpr uint32_t kOffCmcsr = 2u;
constexpr uint32_t kOffCmcnt = 4u;
constexpr uint32_t kOffCmcor = 6u;

constexpr uint16_t kCmstrStr0     = 0x0001u;
constexpr uint16_t kCmstrReserved = 0x0002u;
constexpr uint16_t kCmcsrCmf      = 0x0080u;
constexpr uint16_t kCmcsrReserved = 0x0040u;
constexpr uint16_t kCmcsrCks      = 0x0003u;
constexpr uint16_t kCmcorReset    = 0xFFFFu;

constexpr uint32_t kCksShift[4] = {2u, 3u, 4u, 6u};
constexpr uint64_t kCounterSpan = 0x10000u;

}

void Sh77xxCmt::OnReady() {
    clock_  = &emu_.Get<GuestCycleClock>();
    dmac_   = &emu_.Get<Sh77xxDmac>();
    event_  = clock_->Add([this] {
        Sync();
        Arm();
    });
    pclk_.Attach([this] { Arm(); });
    emu_.Get<Sh77xxStbcr2>().RegisterChangeListener([this](uint8_t, uint8_t) { UpdateCounting(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { PowerOnReset(); });
    PowerOnReset();
    emu_.Get<PeripheralDispatcher>().Register(this);
}

bool Sh77xxCmt::ClockRuns() const { return (cmstr_ & kCmstrStr0) != 0u && !dmac_->ClockStopped(); }

uint64_t Sh77xxCmt::Ticks() const { return pclk_.Ticks() >> kCksShift[cmcsr_ & kCmcsrCks]; }

uint64_t Sh77xxCmt::FirstMatch(uint16_t count) const {
    if (count <= cmcor_) return static_cast<uint64_t>(cmcor_ - count) + 1u;
    return kCounterSpan - count + cmcor_ + 1u;
}

uint16_t Sh77xxCmt::Count() const {
    if (!counting_) return held_count_;
    return static_cast<uint16_t>(anchor_count_ + (Ticks() - anchor_tick_));
}

void Sh77xxCmt::Sync() {
    if (!counting_) return;
    const uint64_t now = Ticks();
    if (now < next_match_) return;
    const uint64_t period = static_cast<uint64_t>(cmcor_) + 1u;
    anchor_tick_          = next_match_ + (now - next_match_) / period * period;
    anchor_count_         = 0u;
    next_match_           = anchor_tick_ + period;
    cmcsr_ |= kCmcsrCmf;
    Arm();
    dmac_->Request(Sh77xxDmaRequest::kCmt);
}

void Sh77xxCmt::Anchor(uint16_t count) {
    anchor_tick_  = Ticks();
    anchor_count_ = count;
    next_match_   = anchor_tick_ + FirstMatch(count);
}

void Sh77xxCmt::Arm() {
    if (!counting_) {
        clock_->Disarm(event_);
        return;
    }
    clock_->Arm(event_, pclk_.CycleOfTick(next_match_ << kCksShift[cmcsr_ & kCmcsrCks]));
}

void Sh77xxCmt::UpdateCounting() {
    const bool runs = ClockRuns();
    if (runs == counting_) return;
    if (runs) {
        counting_ = true;
        Anchor(held_count_);
    } else {
        Sync();
        held_count_ = Count();
        counting_   = false;
    }
    Arm();
}

uint16_t Sh77xxCmt::ReadHalf(uint32_t addr) {
    switch (addr - MmioBase()) {
        case kOffCmstr: return cmstr_;
        case kOffCmcsr:
            Sync();
            if ((cmcsr_ & kCmcsrCmf) != 0u) cmf_read_ = true;
            return cmcsr_;
        case kOffCmcnt: Sync(); return Count();
        case kOffCmcor: return cmcor_;
        default: HaltUnsupportedAccess("ReadHalf", addr, 0u);
    }
}

void Sh77xxCmt::WriteHalf(uint32_t addr, uint16_t value) {
    switch (addr - MmioBase()) {
        case kOffCmstr:
            if ((value & kCmstrReserved) != 0u) {
                emu_.Get<Fatal>().Die("Sh77xxCmt: CMSTR write 0x%04X sets reserved bit 1", value);
            }
            cmstr_ = value & kCmstrStr0;
            UpdateCounting();
            return;
        case kOffCmcsr: WriteControl(value); return;
        case kOffCmcnt:
            if (counting_) {
                Sync();
                Anchor(value);
            } else {
                held_count_ = value;
            }
            Arm();
            return;
        case kOffCmcor:
            if (counting_) {
                Sync();
                const uint16_t count = Count();
                cmcor_               = value;
                Anchor(count);
            } else {
                cmcor_ = value;
            }
            Arm();
            return;
        default: HaltUnsupportedAccess("WriteHalf", addr, value);
    }
}

void Sh77xxCmt::WriteControl(uint16_t value) {
    if ((value & kCmcsrReserved) != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxCmt: CMCSR write 0x%04X sets reserved bit 6", value);
    }
    Sync();
    const uint16_t count = Count();
    bool           cmf   = (cmcsr_ & kCmcsrCmf) != 0u;
    if ((value & kCmcsrCmf) == 0u && cmf_read_) {
        cmf       = false;
        cmf_read_ = false;
    }
    cmcsr_ = static_cast<uint16_t>((value & kCmcsrCks) | (cmf ? kCmcsrCmf : 0u));
    if (counting_) Anchor(count);
    Arm();
}

void Sh77xxCmt::PowerOnReset() {
    cmstr_      = 0u;
    cmcsr_      = 0u;
    cmcor_      = kCmcorReset;
    held_count_ = 0u;
    counting_   = false;
    cmf_read_   = false;
    clock_->Disarm(event_);
}

void Sh77xxCmt::SaveState(StateWriter& w) {
    const uint64_t now = counting_ ? Ticks() : 0u;
    w.Write("cmstr", cmstr_);
    w.Write("cmcsr", cmcsr_);
    w.Write("cmcor", cmcor_);
    w.Write("cmcnt", Count());
    w.Write("ticks_to_match", counting_ && now < next_match_ ? next_match_ - now : uint64_t{0u});
    w.Write("counting", counting_);
    w.Write("cmf_read", cmf_read_);
    pclk_.Save(w);
}

void Sh77xxCmt::RestoreState(StateReader& r) {
    r.Read("cmstr", cmstr_);
    r.Read("cmcsr", cmcsr_);
    r.Read("cmcor", cmcor_);
    r.Read("cmcnt", held_count_);
    r.Read("ticks_to_match", restored_ticks_to_match_);
    r.Read("counting", counting_);
    r.Read("cmf_read", cmf_read_);
    pclk_.Restore(r);
}

void Sh77xxCmt::PostRestore() {
    pclk_.PostRestore();
    if (counting_) {
        anchor_tick_  = Ticks();
        anchor_count_ = held_count_;
        next_match_   = anchor_tick_ + restored_ticks_to_match_;
    }
    Arm();
}

}
