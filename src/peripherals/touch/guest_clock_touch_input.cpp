#include "guest_clock_touch_input.h"

#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

void GuestClockTouchInput::OnReady() {
    samples_.Attach([this] { OnSample(); });
    pens_.Attach(emu_, [this](const HostPen& pen) { ApplyHostPen(pen); });
    OnTouchReady();
}

void GuestClockTouchInput::OnPenDown(int x, int y) {
    pens_.Track(x, y);
    QueueHostPen(HostPen{kPenDown, x, y});
}

void GuestClockTouchInput::OnPenMove(int x, int y) {
    pens_.Track(x, y);
}

void GuestClockTouchInput::OnPenUp(int, int) {
    QueueHostPen(HostPen{kPenUp, 0, 0});
}

void GuestClockTouchInput::OnCaptureLost() {
    QueueHostPen(HostPen{kPenUp, 0, 0});
}

void GuestClockTouchInput::StartSamples(GuestCycleClock::Rate rate) {
    samples_.Start(rate);
}

void GuestClockTouchInput::SetSampleRate(GuestCycleClock::Rate rate) {
    samples_.SetRate(rate);
}

void GuestClockTouchInput::StopSamples() {
    samples_.Stop();
}

void GuestClockTouchInput::ClearOnRestore() {
    StopSamples();
    pens_.Clear();
}

}
