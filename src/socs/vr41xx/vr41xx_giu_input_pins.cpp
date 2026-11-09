#include "vr41xx_giu_input_pins.h"

#include <cerfcore/state/state_stream.h>

namespace cerfcore {

uint32_t Vr41xxGiuInputPins::Levels() const {
    const uint32_t pulled = static_cast<uint32_t>(useupdn_ & termupdn_) & ~driven_;
    return (level_ & driven_) | pulled;
}

uint32_t Vr41xxGiuInputPins::Established() const {
    return driven_ | static_cast<uint32_t>(useupdn_);
}

void Vr41xxGiuInputPins::Drive(int pin, bool level) {
    const uint32_t bit = 1u << pin;
    driven_ |= bit;
    level_ = level ? (level_ | bit) : (level_ & ~bit);
}

void Vr41xxGiuInputPins::Release(int pin) {
    const uint32_t bit = 1u << pin;
    driven_ &= ~bit;
    level_  &= ~bit;
}

void Vr41xxGiuInputPins::ResetPulls() {
    useupdn_  = 0u;
    termupdn_ = 0u;
}

void Vr41xxGiuInputPins::Save(StateWriter& w) const {
    w.Write("level", level_);
    w.Write("driven", driven_);
    if (pull_pins_ == 0u) return;
    w.Write("useupdn", useupdn_);
    w.Write("termupdn", termupdn_);
}

void Vr41xxGiuInputPins::Restore(StateReader& r) {
    r.Read("level", level_);
    r.Read("driven", driven_);
    if (pull_pins_ == 0u) return;
    r.Read("useupdn", useupdn_);
    r.Read("termupdn", termupdn_);
}

}
