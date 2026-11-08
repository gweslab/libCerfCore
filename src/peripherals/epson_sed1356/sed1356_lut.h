#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Sed1356Lut {
public:
    uint8_t Read(uint32_t select);
    void    Write(uint32_t select, uint8_t value);
    void    PointAt(uint8_t index);
    void    ResetPointer();
    void    LcdRgb(uint32_t index, uint8_t& r4, uint8_t& g4, uint8_t& b4) const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    void Step();

    uint8_t lcd_[256][3]  = {};
    uint8_t crt_[256][3]  = {};
    uint8_t index_        = 0;
    uint8_t component_    = 0;
    uint8_t rgb_latch_[2] = {};
};

}
