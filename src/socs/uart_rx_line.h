#pragma once

#include "../execution/guest_cycle_clock.h"
#include "rated_tick_count.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class UartRxLine {
public:
    static constexpr uint64_t kNever = UINT64_MAX;

    struct Frame {
        uint64_t land  = 0;
        uint64_t space = 0;

        bool operator==(const Frame&) const = default;
    };

    explicit UartRxLine(const char* name) : name_(name) {}

    const char* Name() const { return name_; }

    bool                 Push(const uint8_t* data, size_t n);
    std::vector<uint8_t> TakeWire();
    bool                 WireEmpty() const;

    bool StartClock(GuestCycleClock::Rate cpu, GuestCycleClock::Rate bit, uint64_t now);
    bool Receive(const uint8_t* data, size_t n, GuestCycleClock::Rate cpu, GuestCycleClock::Rate bit,
                 const Frame& frame, uint64_t now);
    void Append(uint16_t entry, uint64_t due);
    void PopFront(uint64_t now);
    void Clear();
    void LogNoClock(size_t n) const;
    bool DropNoClock();
    bool Reframe(const Frame& from, const Frame& to);
    bool Rescale(GuestCycleClock::Rate cpu, uint64_t now);
    bool SetBitRate(GuestCycleClock::Rate cpu, GuestCycleClock::Rate bit, uint64_t now, bool& changed);
    void Stop();
    void Reset();

    bool Quiet(uint64_t idle_ticks, uint64_t now) {
        if (!entries_.empty() || !timed_) return false;
        if (!quiet_ && half_.TicksAt(now) >= last_land_ + idle_ticks) quiet_ = true;
        return quiet_;
    }
    uint64_t QuietTick(uint64_t idle_ticks) const {
        if (!timed_) return kNever;
        if (!entries_.empty()) return due_.back() + idle_ticks;
        return quiet_ ? kNever : last_land_ + idle_ticks;
    }

    bool Timed() const { return timed_; }
    bool FrontDue(uint64_t now) const {
        if (next_due_ == kNever) return false;
        return next_due_ == 0u || (timed_ && next_due_ <= half_.TicksAt(now));
    }
    uint16_t Front() const { return entries_.front(); }
    size_t   Pending() const { return entries_.size(); }
    uint64_t DueTick(size_t index) const { return due_[index]; }
    uint64_t CycleOfTick(uint64_t tick) const { return half_.CycleOfTick(tick); }
    uint64_t FrontLandCycle(uint64_t now) const {
        return FrontDue(now) ? now : half_.CycleOfTick(next_due_);
    }

    void Save(StateWriter& w, uint64_t now) const;
    void Restore(StateReader& r, GuestCycleClock::Rate cpu, uint64_t now);

private:
    const char*           name_;
    std::deque<uint16_t>  entries_;
    std::deque<uint64_t>  due_;
    uint64_t              next_due_ = kNever;
    NarrowRatedTickCount  half_;
    GuestCycleClock::Rate half_rate_{};
    bool                  timed_      = false;
    uint64_t              next_start_ = 0;
    uint64_t              last_land_  = 0;
    bool                  quiet_      = true;
    mutable std::mutex    wire_mtx_;
    std::vector<uint8_t>  wire_;
};

}
