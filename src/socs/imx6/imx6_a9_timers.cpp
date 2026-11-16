#include "imx6_a9_timers.h"

#include "../../core/tick_scale.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace imx6_gic_detail {

namespace {

constexpr uint32_t kPtEnable     = 1u << 0;
constexpr uint32_t kPtAutoReload = 1u << 1;
constexpr uint32_t kPtIrqEnable  = 1u << 2;

constexpr uint32_t kGtEnable     = 1u << 0;
constexpr uint32_t kGtCompEnable = 1u << 1;
constexpr uint32_t kGtIrqEnable  = 1u << 2;
constexpr uint32_t kGtAutoInc    = 1u << 3;

constexpr uint32_t kEventFlag = 1u << 0;

}

void Imx6A9Timers::Attach(GuestCycleClock* clock, GuestCycleClock::Event* private_event,
                          GuestCycleClock::Event* global_event) {
    clock_         = clock;
    private_event_ = private_event;
    global_event_  = global_event;
}

void Imx6A9Timers::SetTickScale() {
    TickScale(pt_control_, pt_tick_num_, pt_tick_den_);
    TickScale(gt_control_, gt_tick_num_, gt_tick_den_);
}

void Imx6A9Timers::TickScale(uint32_t control, uint64_t& num, uint64_t& den) const {
    num = 1u;
    den = 2u * (((control >> 8) & 0xFFu) + 1u);
}

uint64_t Imx6A9Timers::TicksAt(uint64_t cycles, uint64_t anchor, uint64_t phase,
                               uint64_t num, uint64_t den) const {
    uint64_t rem = 0u;
    const uint64_t ticks = MulDivU64(cycles - anchor, num, den, rem);
    const uint64_t accumulated = rem + phase;
    return ticks + accumulated / den;
}

uint64_t Imx6A9Timers::CyclesForTicks(uint64_t ticks, uint64_t phase,
                                      uint64_t num, uint64_t den) const {
    uint64_t rem = 0u;
    const uint64_t cycles = MulDivU64(ticks, den, num, rem);
    const uint64_t phase_whole = phase / num;
    const uint64_t phase_rem = phase % num;
    return cycles - phase_whole + (rem > phase_rem ? 1u : 0u);
}

uint32_t Imx6A9Timers::PrivateCountAt(uint64_t now) const {
    if (clock_gated_) return pt_count_;
    if ((pt_control_ & kPtEnable) == 0u) return pt_count_;
    const uint64_t n = TicksAt(now, pt_anchor_, pt_phase_, pt_tick_num_, pt_tick_den_);
    if (n <= pt_count_) return static_cast<uint32_t>(pt_count_ - n);
    if ((pt_control_ & kPtAutoReload) == 0u) return 0u;
    const uint64_t period = static_cast<uint64_t>(pt_load_) + 1u;
    return static_cast<uint32_t>(pt_load_ - (n - pt_count_ - 1u) % period);
}

uint64_t Imx6A9Timers::PrivatePhaseAt(uint64_t now) const {
    if (clock_gated_) return pt_phase_;
    if ((pt_control_ & kPtEnable) == 0u) return 0u;
    uint64_t rem = 0u;
    MulDivU64(now - pt_anchor_, pt_tick_num_, pt_tick_den_, rem);
    return (rem + pt_phase_) % pt_tick_den_;
}

void Imx6A9Timers::RebasePrivate(uint64_t now, uint32_t count, uint64_t phase) {
    pt_count_  = count;
    pt_anchor_ = now;
    pt_phase_  = phase % pt_tick_den_;
    SetPrivateNext(count);
    ArmPrivate();
}

void Imx6A9Timers::SetPrivateNext(uint32_t count) {
    pt_armed_  = true;
    if (count != 0u) pt_next_ = count;
    else if ((pt_control_ & kPtAutoReload) != 0u) pt_next_ = static_cast<uint64_t>(pt_load_) + 1u;
    else pt_armed_ = false;
}

void Imx6A9Timers::ArmPrivate() {
    if (clock_gated_ || !pt_armed_ ||
        (pt_control_ & (kPtEnable | kPtIrqEnable)) != (kPtEnable | kPtIrqEnable)) {
        clock_->Disarm(private_event_);
        return;
    }
    clock_->Arm(private_event_, pt_anchor_ + CyclesForTicks(pt_next_, pt_phase_, pt_tick_num_, pt_tick_den_));
}

uint64_t Imx6A9Timers::GlobalCountAt(uint64_t now) const {
    if (clock_gated_) return gt_count_;
    if ((gt_control_ & kGtEnable) == 0u) return gt_count_;
    return gt_count_ + TicksAt(now, gt_anchor_, gt_phase_, gt_tick_num_, gt_tick_den_);
}

uint64_t Imx6A9Timers::GlobalPhaseAt(uint64_t now) const {
    if (clock_gated_) return gt_phase_;
    if ((gt_control_ & kGtEnable) == 0u) return 0u;
    uint64_t rem = 0u;
    MulDivU64(now - gt_anchor_, gt_tick_num_, gt_tick_den_, rem);
    return (rem + gt_phase_) % gt_tick_den_;
}

void Imx6A9Timers::RebaseGlobal(uint64_t now) {
    if ((gt_control_ & kGtEnable) == 0u) {
        gt_anchor_ = now;
        gt_phase_ = 0u;
        return;
    }
    uint64_t rem = 0u;
    const uint64_t n = MulDivU64(now - gt_anchor_, gt_tick_num_, gt_tick_den_, rem);
    const uint64_t accumulated = rem + gt_phase_;
    gt_count_ += n + accumulated / gt_tick_den_;
    gt_phase_ = accumulated % gt_tick_den_;
    gt_anchor_ = now;
}

void Imx6A9Timers::ArmGlobal(uint64_t now) {
    constexpr uint32_t kArmed = kGtEnable | kGtCompEnable | kGtIrqEnable;
    if (clock_gated_ || gt_fired_ || (gt_control_ & kArmed) != kArmed) {
        clock_->Disarm(global_event_);
        return;
    }
    const uint64_t count = GlobalCountAt(now);
    if (gt_compare_ <= count) {
        clock_->Arm(global_event_, now);
        return;
    }
    RebaseGlobal(now);
    clock_->Arm(global_event_, gt_anchor_ + CyclesForTicks(gt_compare_ - gt_count_, gt_phase_,
                                                           gt_tick_num_, gt_tick_den_));
}

uint32_t Imx6A9Timers::CatchUp() {
    if (clock_gated_) return 0u;
    uint32_t irq_events = 0u;
    const uint64_t now = clock_->Cycles();
    if (pt_armed_ && (pt_control_ & kPtEnable) != 0u) {
        const uint64_t n = TicksAt(now, pt_anchor_, pt_phase_, pt_tick_num_, pt_tick_den_);
        if (n >= pt_next_) {
            if ((pt_control_ & kPtIrqEnable) != 0u) irq_events |= 1u << 29;
            pt_status_ |= kEventFlag;
            if ((pt_control_ & kPtAutoReload) != 0u) {
                const uint64_t period = static_cast<uint64_t>(pt_load_) + 1u;
                pt_next_ += ((n - pt_next_) / period + 1u) * period;
            } else {
                pt_armed_ = false;
            }
            ArmPrivate();
        }
    }
    if (!gt_fired_ && (gt_control_ & (kGtEnable | kGtCompEnable)) == (kGtEnable | kGtCompEnable)) {
        const uint64_t count = GlobalCountAt(now);
        if (gt_compare_ <= count) {
            if ((gt_control_ & kGtIrqEnable) != 0u) irq_events |= 1u << 27;
            gt_status_ |= kEventFlag;
            if ((gt_control_ & kGtAutoInc) != 0u && gt_increment_ != 0u)
                gt_compare_ += ((count - gt_compare_) / gt_increment_ + 1u) * gt_increment_;
            else
                gt_fired_ = true;
            ArmGlobal(now);
        }
    }
    return irq_events;
}

bool Imx6A9Timers::Read(uint32_t off, uint32_t& value) {
    const uint64_t now = clock_->Cycles();
    switch (off) {
        case 0x200: value = static_cast<uint32_t>(GlobalCountAt(now)); return true;
        case 0x204: value = static_cast<uint32_t>(GlobalCountAt(now) >> 32); return true;
        case 0x208: value = gt_control_; return true;
        case 0x20C: value = gt_status_; return true;
        case 0x210: value = static_cast<uint32_t>(gt_compare_); return true;
        case 0x214: value = static_cast<uint32_t>(gt_compare_ >> 32); return true;
        case 0x218: value = gt_increment_; return true;
        case 0x600: value = pt_load_; return true;
        case 0x604: value = PrivateCountAt(now); return true;
        case 0x608: value = pt_control_; return true;
        case 0x60C: value = pt_status_; return true;
        default: return false;
    }
}

bool Imx6A9Timers::Write(uint32_t off, uint32_t value) {
    const uint64_t now = clock_->Cycles();
    switch (off) {
        case 0x200:
        case 0x204:
            RebaseGlobal(now);
            gt_count_ = off == 0x200u ? (gt_count_ & 0xFFFFFFFF00000000ull) | value
                                      : (gt_count_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
            gt_anchor_ = now;
            gt_fired_  = false;
            ArmGlobal(now);
            return true;
        case 0x208: {
            RebaseGlobal(now);
            const uint64_t old_den = gt_tick_den_;
            const uint64_t phase = gt_phase_;
            gt_control_ = value;
            SetTickScale();
            gt_anchor_ = now;
            gt_phase_ = (gt_control_ & kGtEnable) != 0u
                            ? MulDivU64(phase, gt_tick_den_, old_den)
                            : 0u;
            gt_fired_ = false;
            ArmGlobal(now);
            return true;
        }
        case 0x20C: gt_status_ &= ~value; return true;
        case 0x210:
        case 0x214:
            gt_compare_ = off == 0x210u ? (gt_compare_ & 0xFFFFFFFF00000000ull) | value
                                        : (gt_compare_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
            gt_fired_ = false;
            ArmGlobal(now);
            return true;
        case 0x218: gt_increment_ = value; return true;
        case 0x600:
            pt_load_ = value;
            RebasePrivate(now, value);
            return true;
        case 0x604: RebasePrivate(now, value); return true;
        case 0x608: {
            const uint32_t count = PrivateCountAt(now);
            const uint64_t old_den = pt_tick_den_;
            const uint64_t phase = PrivatePhaseAt(now);
            pt_control_ = value;
            SetTickScale();
            RebasePrivate(now, count, (pt_control_ & kPtEnable) != 0u
                                          ? MulDivU64(phase, pt_tick_den_, old_den)
                                          : 0u);
            return true;
        }
        case 0x60C: pt_status_ &= ~value; return true;
        default: return false;
    }
}

void Imx6A9Timers::Reset() {
    pt_load_ = pt_control_ = pt_status_ = pt_count_ = 0u;
    pt_anchor_ = pt_next_ = 0u;
    pt_armed_ = false;
    gt_count_ = gt_anchor_ = gt_compare_ = 0u;
    gt_control_ = gt_status_ = gt_increment_ = 0u;
    gt_fired_ = false;
    clock_gated_ = false;
    SetTickScale();
    clock_->Disarm(private_event_);
    clock_->Disarm(global_event_);
}

void Imx6A9Timers::SaveState(StateWriter& w) {
    CatchUp();
    const uint64_t now = clock_->Cycles();
    w.Write("pt_load", pt_load_);
    w.Write("pt_control", pt_control_);
    w.Write("pt_status", pt_status_);
    w.Write("pt_count", PrivateCountAt(now));
    w.Write<uint64_t>("pt_phase", PrivatePhaseAt(now));
    w.Write("gt_count", GlobalCountAt(now));
    w.Write<uint64_t>("gt_phase", GlobalPhaseAt(now));
    w.Write("gt_control", gt_control_);
    w.Write("gt_status", gt_status_);
    w.Write("gt_compare", gt_compare_);
    w.Write("gt_increment", gt_increment_);
    w.Write<uint8_t>("gt_fired", gt_fired_ ? 1u : 0u);
}

void Imx6A9Timers::RestoreState(StateReader& r) {
    uint8_t gt_fired = 0u;
    r.Read("pt_load", pt_load_);
    r.Read("pt_control", pt_control_);
    r.Read("pt_status", pt_status_);
    r.Read("pt_count", pt_count_);
    r.Read("pt_phase", pt_phase_);
    r.Read("gt_count", gt_count_);
    r.Read("gt_phase", gt_phase_);
    r.Read("gt_control", gt_control_);
    r.Read("gt_status", gt_status_);
    r.Read("gt_compare", gt_compare_);
    r.Read("gt_increment", gt_increment_);
    r.Read("gt_fired", gt_fired);
    gt_fired_ = gt_fired != 0u;
    clock_gated_ = false;
    SetTickScale();
    clock_->Disarm(private_event_);
    clock_->Disarm(global_event_);
}

void Imx6A9Timers::Resume() {
    const uint64_t now = clock_->Cycles();
    pt_anchor_ = now;
    gt_anchor_ = now;
    SetPrivateNext(pt_count_);
    ArmPrivate();
    ArmGlobal(now);
}

void Imx6A9Timers::SuspendForGatedClock() {
    if (clock_gated_) return;
    const uint64_t now = clock_->Cycles();
    pt_count_ = PrivateCountAt(now);
    pt_phase_ = PrivatePhaseAt(now);
    gt_count_ = GlobalCountAt(now);
    gt_phase_ = GlobalPhaseAt(now);
    pt_anchor_ = now;
    gt_anchor_ = now;
    clock_gated_ = true;
    clock_->Disarm(private_event_);
    clock_->Disarm(global_event_);
}

void Imx6A9Timers::ResumeFromGatedClock() {
    if (!clock_gated_) return;
    clock_gated_ = false;
    Resume();
}

}

}
