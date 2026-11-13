#pragma once

#include "../external_device_clock.h"
#include "serial_line_timer.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class CerfEmulator;
class StateWriter;
class StateReader;

class SerialDeviceClockTimer : public SerialLineTimer {
public:
    SerialDeviceClockTimer(CerfEmulator& emu, std::function<void()> on_due);

    void AttachLineTimer();

    void ArmLineTimer(int64_t delay_ns) override;
    void DisarmLineTimer() override;

    void SaveLineTimer(StateWriter& w, const char* key) const;
    void RestoreLineTimer(StateReader& r, const char* key);
    void ResumeLineTimer();

private:
    CerfEmulator&               line_timer_emu_;
    std::function<void()>       line_timer_due_;
    ExternalDeviceClock::Timer* line_timer_ = nullptr;
};

}
