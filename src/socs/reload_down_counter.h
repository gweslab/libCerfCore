#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class ReloadDownCounter {
public:
    enum class FlagPoint : uint8_t { kReload, kZero };

    explicit ReloadDownCounter(FlagPoint point) : zero_flag_(point == FlagPoint::kZero) {}

    void PowerOnReset(uint32_t value);

    bool     Running() const { return running_; }
    bool     Flagged() const { return flag_; }
    uint32_t Constant() const { return constant_; }
    uint64_t NextFlag() const;

    void     ClearFlag() { flag_ = false; }
    void     Advance(uint64_t tick);
    uint32_t Count(uint64_t tick);
    void     Start(uint64_t tick);
    void     Rebase(uint64_t tick);
    void     Stop(uint64_t tick);
    void     Move(uint64_t from_tick, uint64_t to_tick);
    void     WriteCount(uint64_t tick, uint32_t value);
    void     WriteConstant(uint64_t tick, uint32_t value);

    void Save(StateWriter& w, const char* prefix, uint64_t tick);
    void Restore(StateReader& r, const char* prefix);

private:
    void     Load(uint64_t tick, uint32_t value);
    uint64_t Period() const { return static_cast<uint64_t>(constant_) + 1u; }
    uint64_t FirstFlag() const { return reload_ - (zero_flag_ ? 1u : 0u); }

    const bool zero_flag_;
    uint64_t   anchor_   = 0u;
    uint64_t   reload_   = 0u;
    uint64_t   consumed_ = 0u;
    uint32_t   value_    = 0u;
    uint32_t   constant_ = 0u;
    uint32_t   stopped_  = 0u;
    bool       running_  = false;
    bool       flag_     = false;
};

}
