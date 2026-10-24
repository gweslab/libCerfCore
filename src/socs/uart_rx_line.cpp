#define NOMINMAX

#include "uart_rx_line.h"

#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

namespace {

constexpr size_t kWireMax = 64u * 1024u;

}

bool UartRxLine::Push(const uint8_t* data, size_t n) {
    std::lock_guard<std::mutex> lk(wire_mtx_);
    if (wire_.size() + n > kWireMax) {
        LOG(Caution, "[UART] %s RX: the guest is not taking received bytes, dropping %zu\n", name_,
            n);
        return false;
    }
    wire_.insert(wire_.end(), data, data + n);
    return true;
}

std::vector<uint8_t> UartRxLine::TakeWire() {
    std::vector<uint8_t> bytes;
    std::lock_guard<std::mutex> lk(wire_mtx_);
    bytes.swap(wire_);
    return bytes;
}

bool UartRxLine::WireEmpty() const {
    std::lock_guard<std::mutex> lk(wire_mtx_);
    return wire_.empty();
}

bool UartRxLine::StartClock(GuestCycleClock::Rate cpu, GuestCycleClock::Rate bit, uint64_t now) {
    if (timed_ && (!entries_.empty() || next_start_ > half_.TicksAt(now))) return true;
    half_rate_ = bit.Scaled(2u, 1u);
    if (!half_.SetRate(cpu, half_rate_)) return false;
    half_.Start(now);
    timed_      = true;
    next_start_ = 0u;
    last_land_  = 0u;
    return true;
}

bool UartRxLine::Receive(const uint8_t* data, size_t n, GuestCycleClock::Rate cpu,
                         GuestCycleClock::Rate bit, const Frame& frame, uint64_t now) {
    if (entries_.size() + n > kWireMax) {
        LOG(Caution, "[UART] %s RX: the guest is not taking received bytes, dropping %zu\n", name_,
            n);
        return true;
    }
    if (!StartClock(cpu, bit, now)) return false;
    uint64_t start = std::max(next_start_, half_.TicksAt(now));
    for (size_t i = 0; i < n; ++i) {
        Append(data[i], start + frame.land);
        start += frame.space;
    }
    next_start_ = start;
    return true;
}

void UartRxLine::Append(uint16_t entry, uint64_t due) {
    if (entries_.empty()) next_due_ = due;
    entries_.push_back(entry);
    due_.push_back(due);
}

void UartRxLine::PopFront(uint64_t now) {
    last_land_ = next_due_ != 0u ? next_due_ : half_.TicksAt(now);
    quiet_     = false;
    entries_.pop_front();
    due_.pop_front();
    next_due_ = due_.empty() ? kNever : due_.front();
}

void UartRxLine::Clear() {
    entries_.clear();
    due_.clear();
    next_due_ = kNever;
}

void UartRxLine::LogNoClock(size_t n) const {
    LOG(Caution, "[UART] %s RX: no sample clock, %zu received bytes not latched\n", name_, n);
}

bool UartRxLine::DropNoClock() {
    if (entries_.empty()) return false;
    LogNoClock(entries_.size());
    Clear();
    return true;
}

bool UartRxLine::Reframe(const Frame& from, const Frame& to) {
    if (from == to) return false;
    bool     any      = false;
    uint64_t prev_old = 0, prev_new = 0;
    for (size_t i = 0; i < due_.size(); ++i) {
        if (due_[i] == 0u) continue;
        const uint64_t start_old = due_[i] - from.land;
        uint64_t start_new = start_old;
        if (any) {
            start_new = start_old == prev_old + from.space ? prev_new + to.space
                                                           : std::max(start_old, prev_new + to.space);
        }
        due_[i]  = start_new + to.land;
        prev_old = start_old;
        prev_new = start_new;
        any      = true;
    }
    if (!any) return false;
    next_start_ = prev_new + to.space;
    next_due_   = due_.front();
    return true;
}

bool UartRxLine::Rescale(GuestCycleClock::Rate cpu, uint64_t now) {
    if (!timed_) return true;
    return half_.Rescale(now, cpu, half_rate_);
}

bool UartRxLine::SetBitRate(GuestCycleClock::Rate cpu, GuestCycleClock::Rate bit, uint64_t now,
                            bool& changed) {
    const GuestCycleClock::Rate half = bit.Scaled(2u, 1u);
    changed = false;
    if (half == half_rate_) return true;
    half_rate_ = half;
    changed    = true;
    return Rescale(cpu, now);
}

void UartRxLine::Stop() {
    Clear();
    timed_      = false;
    next_start_ = 0u;
    last_land_  = 0u;
    quiet_      = true;
}

void UartRxLine::Reset() {
    {
        std::lock_guard<std::mutex> lk(wire_mtx_);
        wire_.clear();
    }
    Stop();
}

void UartRxLine::Save(StateWriter& w, uint64_t now) const {
    const TickPosition pos = timed_ ? half_.PositionAt(now) : TickPosition{};
    w.Write<uint8_t>("rx_line_timed", timed_ ? 1u : 0u);
    w.Write<uint64_t>("rx_line_half_ticks", pos.ticks);
    w.Write<uint64_t>("rx_line_half_phase", pos.phase);
    w.Write<uint64_t>("rx_line_half_phase_den", pos.phase_den);
    w.Write<uint64_t>("rx_line_half_rate_num", half_rate_.num);
    w.Write<uint64_t>("rx_line_half_rate_den", half_rate_.den);
    w.Write<uint64_t>("rx_line_next_start", next_start_);
    w.Write<uint64_t>("rx_line_last_land", last_land_);
    w.Write<uint8_t>("rx_line_quiet", quiet_ ? 1u : 0u);
    w.Write<uint32_t>("rx_line_count", static_cast<uint32_t>(entries_.size()));
    for (size_t i = 0; i < entries_.size(); ++i) {
        w.Write("rx_line_entry", entries_[i]);
        w.Write("rx_line_due", due_[i]);
    }
}

void UartRxLine::Restore(StateReader& r, GuestCycleClock::Rate cpu, uint64_t now) {
    uint8_t      timed = 0, quiet = 0;
    uint32_t     n = 0;
    TickPosition pos;
    r.Read("rx_line_timed", timed);
    r.Read("rx_line_half_ticks", pos.ticks);
    r.Read("rx_line_half_phase", pos.phase);
    r.Read("rx_line_half_phase_den", pos.phase_den);
    r.Read("rx_line_half_rate_num", half_rate_.num);
    r.Read("rx_line_half_rate_den", half_rate_.den);
    r.Read("rx_line_next_start", next_start_);
    r.Read("rx_line_last_land", last_land_);
    r.Read("rx_line_quiet", quiet);
    Clear();
    r.Read("rx_line_count", n);
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t e = 0;
        uint64_t d = 0;
        r.Read("rx_line_entry", e);
        r.Read("rx_line_due", d);
        Append(e, d);
    }
    {
        std::lock_guard<std::mutex> lk(wire_mtx_);
        wire_.clear();
    }
    timed_ = timed != 0u;
    quiet_ = quiet != 0u;
    if (!timed_) return;
    half_.SetRate(cpu, half_rate_);
    half_.PlaceAt(now, pos);
}

}
