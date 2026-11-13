#pragma once

#include <chrono>
#include <cstdint>

namespace cerfcore {

inline uint64_t HostSteadyMicros() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

inline int64_t HostSteadyNanos() {
    using namespace std::chrono;
    return static_cast<int64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

inline bool HostSteadySquareWaveHigh(uint64_t half_period_ms) {
    return (HostSteadyMicros() / 1000u / half_period_ms) % 2u == 0u;
}

}
