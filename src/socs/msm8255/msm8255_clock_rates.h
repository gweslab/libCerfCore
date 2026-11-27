#pragma once

#include <cerfcore/core/service.h>

#include <atomic>
#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

inline constexpr uint32_t kMsm8255MdhIndexCount = 2u;
inline constexpr uint32_t kMsm8255CameraClock   = 65u;
inline constexpr uint32_t kMsm8255PmdhClock     = 115u;
inline constexpr uint32_t kMsm8255MdpVsyncClock = 43u;
inline constexpr uint32_t kMsm8255MdpVsyncHz    = 24576000u;
inline constexpr uint32_t kMsm8255RatedClockCount = 5u;

class Msm8255ClockRates : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t GrantMdhRateKhz(uint32_t index, uint32_t min_khz,
                             uint32_t max_khz);
    uint32_t GrantClockFreqHz(uint32_t clock, uint32_t freq_hz,
                              uint32_t match);
    uint32_t ReportClockFreqKhz(uint32_t clock);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    uint32_t SelectRateHz(const uint32_t* rates, uint32_t count,
                          uint32_t clock, uint32_t freq_hz, uint32_t match);

    std::atomic<uint32_t> mdh_granted_khz_[kMsm8255MdhIndexCount] = {};
    std::atomic<uint32_t> rated_hz_[kMsm8255RatedClockCount] = {};
    std::atomic<bool>     rated_granted_[kMsm8255RatedClockCount] = {};
};

}
