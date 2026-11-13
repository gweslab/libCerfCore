#include "serial_device_clock_timer.h"

#include <cerfcore/core/cerf_emulator.h>

#include <utility>

namespace cerfcore {

SerialDeviceClockTimer::SerialDeviceClockTimer(CerfEmulator& emu, std::function<void()> on_due)
    : line_timer_emu_(emu), line_timer_due_(std::move(on_due)) {}

void SerialDeviceClockTimer::AttachLineTimer() {
    line_timer_ = &line_timer_emu_.Get<ExternalDeviceClock>().AddTimer([this] { line_timer_due_(); });
}

void SerialDeviceClockTimer::ArmLineTimer(int64_t delay_ns) {
    line_timer_emu_.Get<ExternalDeviceClock>().Arm(*line_timer_, delay_ns);
}

void SerialDeviceClockTimer::DisarmLineTimer() { line_timer_emu_.Get<ExternalDeviceClock>().Disarm(*line_timer_); }

void SerialDeviceClockTimer::SaveLineTimer(StateWriter& w, const char* key) const {
    line_timer_emu_.Get<ExternalDeviceClock>().SaveTimer(*line_timer_, w, key);
}

void SerialDeviceClockTimer::RestoreLineTimer(StateReader& r, const char* key) {
    line_timer_emu_.Get<ExternalDeviceClock>().RestoreTimer(*line_timer_, r, key);
}

void SerialDeviceClockTimer::ResumeLineTimer() {
    line_timer_emu_.Get<ExternalDeviceClock>().ResumeTimer(*line_timer_);
}

}
