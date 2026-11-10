#include "reload_down_counter.h"

#include <cerfcore/state/state_stream.h>

#include <string>

namespace cerfcore {

namespace {

std::string Field(const char* prefix, const char* name) { return std::string(prefix) + name; }

}

void ReloadDownCounter::PowerOnReset(uint32_t value) {
    anchor_   = 0u;
    reload_   = 0u;
    consumed_ = 0u;
    value_    = value;
    constant_ = value;
    stopped_  = value;
    running_  = false;
    flag_     = false;
}

void ReloadDownCounter::Load(uint64_t tick, uint32_t value) {
    anchor_   = tick;
    value_    = value;
    reload_   = tick + static_cast<uint64_t>(value) + 1u;
    consumed_ = tick;
}

uint64_t ReloadDownCounter::NextFlag() const {
    const uint64_t first = FirstFlag();
    if (first > consumed_) return first;
    return first + ((consumed_ - first) / Period() + 1u) * Period();
}

void ReloadDownCounter::Advance(uint64_t tick) {
    if (!running_ || tick <= consumed_) return;
    const uint64_t period = Period();
    const uint64_t first  = FirstFlag();
    if (tick >= first && first + (tick - first) / period * period > consumed_) flag_ = true;
    if (tick >= reload_) {
        anchor_ = reload_ + (tick - reload_) / period * period;
        value_  = constant_;
        reload_ = anchor_ + period;
    }
    consumed_ = tick;
}

uint32_t ReloadDownCounter::Count(uint64_t tick) {
    Advance(tick);
    if (!running_) return stopped_;
    return value_ - static_cast<uint32_t>(tick - anchor_);
}

void ReloadDownCounter::Start(uint64_t tick) {
    running_ = true;
    Rebase(tick);
}

void ReloadDownCounter::Rebase(uint64_t tick) {
    if (running_) Load(tick, stopped_);
}

void ReloadDownCounter::Stop(uint64_t tick) {
    stopped_ = Count(tick);
    running_ = false;
}

void ReloadDownCounter::Move(uint64_t from_tick, uint64_t to_tick) {
    if (!running_) return;
    Load(to_tick, Count(from_tick));
}

void ReloadDownCounter::WriteCount(uint64_t tick, uint32_t value) {
    if (!running_) {
        stopped_ = value;
        return;
    }
    Advance(tick);
    Load(tick, value);
}

void ReloadDownCounter::WriteConstant(uint64_t tick, uint32_t value) {
    Advance(tick);
    constant_ = value;
}

void ReloadDownCounter::Save(StateWriter& w, const char* prefix, uint64_t tick) {
    w.Write(Field(prefix, "count").c_str(), Count(tick));
    w.Write(Field(prefix, "constant").c_str(), constant_);
    w.Write(Field(prefix, "running").c_str(), running_);
    w.Write(Field(prefix, "flag").c_str(), flag_);
}

void ReloadDownCounter::Restore(StateReader& r, const char* prefix) {
    r.Read(Field(prefix, "count").c_str(), stopped_);
    r.Read(Field(prefix, "constant").c_str(), constant_);
    r.Read(Field(prefix, "running").c_str(), running_);
    r.Read(Field(prefix, "flag").c_str(), flag_);
}

}
