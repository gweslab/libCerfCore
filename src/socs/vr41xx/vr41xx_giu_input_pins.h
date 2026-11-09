#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Vr41xxGiuInputPins {
public:
    explicit Vr41xxGiuInputPins(uint16_t pull_pins) : pull_pins_(pull_pins) {}

    uint32_t Levels() const;
    uint32_t Established() const;
    void     Drive(int pin, bool level);
    void     Release(int pin);

    uint16_t UseUpdn() const { return useupdn_; }
    uint16_t TermUpdn() const { return termupdn_; }
    void     SetUseUpdn(uint16_t value) { useupdn_ = value; }
    void     SetTermUpdn(uint16_t value) { termupdn_ = value; }
    void     ResetPulls();

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    uint16_t pull_pins_;
    uint32_t level_    = 0u;
    uint32_t driven_   = 0u;
    uint16_t useupdn_  = 0u;
    uint16_t termupdn_ = 0u;
};

}
