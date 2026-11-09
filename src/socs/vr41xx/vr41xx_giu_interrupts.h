#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Vr41xxGiuInterrupts {
public:
    explicit Vr41xxGiuInterrupts(bool intstat_sets_while_disabled)
        : intstat_sets_while_disabled_(intstat_sets_while_disabled) {}

    void OnPinLevel(int pin, bool prev, bool level);
    void EvaluateLevels(uint32_t pins, uint32_t levels);

    bool ReadRegister(uint32_t off, uint16_t* value) const;
    bool WriteRegister(uint32_t off, uint16_t value);
    void Reset();

    uint16_t IcuLow(bool enable_gates) const;
    uint16_t IcuHigh(bool enable_gates) const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    void ApplyCondition(int pin, bool met);

    bool     intstat_sets_while_disabled_;
    uint16_t intstat_l_  = 0, intstat_h_  = 0;
    uint16_t inten_l_    = 0, inten_h_    = 0;
    uint16_t inttyp_l_   = 0, inttyp_h_   = 0;
    uint16_t intalsel_l_ = 0, intalsel_h_ = 0;
    uint16_t inthtsel_l_ = 0, inthtsel_h_ = 0;
    uint16_t retained_l_ = 0, retained_h_ = 0;
};

}
