#include "vr41xx_audio_switch.h"

#include <cerfcore/core/cerf_emulator.h>

#include <algorithm>
#include <utility>

namespace cerfcore {

bool Vr41xxGpioAudioSwitch::SpeakerOn() { return PinsOn(SpeakerPins()); }

bool Vr41xxGpioAudioSwitch::PinAt(int gpio, Vr41xxPinDrive level) {
    return emu_.Get<Vr41xxGiu>().OutputDrive(gpio) == level;
}

bool Vr41xxGpioAudioSwitch::PinsOn(std::span<const Vr41xxAudioPin> pins) {
    return std::all_of(pins.begin(), pins.end(),
                       [this](const Vr41xxAudioPin& pin) { return PinAt(pin.gpio, pin.on_level); });
}

void Vr41xxGpioAudioSwitch::RegisterChangeListener(std::function<void()> fn) {
    emu_.Get<Vr41xxGiu>().RegisterOutputDriveListener(std::move(fn));
}

}
