#pragma once

#include <cstdint>

namespace cerfcore {

class GuestCycleClock;
class StateReader;
class StateWriter;
class Vr41xxRtcxTicks;

class Vr41xxPiuScanTiming {
public:
    static constexpr uint16_t kIdle = 0, kData = 1, kCmd = 2, kAdPort = 3;

    void Attach(GuestCycleClock* clock, Vr41xxRtcxTicks* rtcx);

    void     Begin(uint16_t kind, uint16_t stable, uint64_t start_tick, uint64_t start_cycle);
    uint16_t Take();
    void     Drop() { kind_ = kIdle; }
    void     Retime(uint64_t now);

    uint16_t Kind() const { return kind_; }
    uint64_t ReadyAt() const { return ready_cycle_; }

    uint64_t DataScanCycles(uint16_t stable) const;
    uint64_t SettleCycles(uint16_t stable) const;
    uint64_t AdPortScanCycles() const;

    void Save(StateWriter& w, uint64_t now) const;
    void Restore(StateReader& r);

private:
    uint64_t ReadyCycle(uint16_t kind, uint16_t stable, uint64_t start_tick, uint64_t start_cycle);
    void     SetReady(uint64_t cycle);
    uint64_t TicksToCycles(uint64_t ticks) const;

    GuestCycleClock* clock_       = nullptr;
    Vr41xxRtcxTicks* rtcx_        = nullptr;
    uint16_t         kind_        = kIdle;
    uint64_t         ready_cycle_ = 0;
    int64_t          ready_ns_    = 0;
};

}
