#include "sed1356_lut.h"

#include <cerfcore/state/state_stream.h>

namespace cerfcore {

uint8_t Sed1356Lut::Read(uint32_t select) {
    const uint8_t (*lut)[3] = (select == 0x2u) ? crt_ : lcd_;
    const uint8_t v = static_cast<uint8_t>(lut[index_][component_] << 4);
    Step();
    return v;
}

void Sed1356Lut::Write(uint32_t select, uint8_t value) {
    const uint8_t comp = static_cast<uint8_t>(value >> 4);
    if (component_ < 2) {
        rgb_latch_[component_] = comp;
    } else {
        const bool to_lcd = (select == 0x0u || select == 0x1u);
        const bool to_crt = (select == 0x0u || select == 0x2u);
        if (to_lcd) {
            lcd_[index_][0] = rgb_latch_[0];
            lcd_[index_][1] = rgb_latch_[1];
            lcd_[index_][2] = comp;
        }
        if (to_crt) {
            crt_[index_][0] = rgb_latch_[0];
            crt_[index_][1] = rgb_latch_[1];
            crt_[index_][2] = comp;
        }
    }
    Step();
}

void Sed1356Lut::PointAt(uint8_t index) {
    index_     = index;
    component_ = 0;
}

void Sed1356Lut::ResetPointer() {
    index_        = 0;
    component_    = 0;
    rgb_latch_[0] = rgb_latch_[1] = 0;
}

void Sed1356Lut::LcdRgb(uint32_t index, uint8_t& r4, uint8_t& g4, uint8_t& b4) const {
    r4 = lcd_[index & 0xFFu][0];
    g4 = lcd_[index & 0xFFu][1];
    b4 = lcd_[index & 0xFFu][2];
}

void Sed1356Lut::Step() {
    if (++component_ == 3) {
        component_ = 0;
        index_++;
    }
}

void Sed1356Lut::SaveState(StateWriter& w) const {
    w.WriteBytes("lcd_lut", lcd_, sizeof(lcd_));
    w.WriteBytes("crt_lut", crt_, sizeof(crt_));
    w.Write("lut_index", index_);
    w.Write("lut_component", component_);
    w.WriteBytes("lut_rgb_latch", rgb_latch_, sizeof(rgb_latch_));
}

void Sed1356Lut::RestoreState(StateReader& r) {
    r.ReadBytes("lcd_lut", lcd_, sizeof(lcd_));
    r.ReadBytes("crt_lut", crt_, sizeof(crt_));
    r.Read("lut_index", index_);
    r.Read("lut_component", component_);
    r.ReadBytes("lut_rgb_latch", rgb_latch_, sizeof(rgb_latch_));
}

}
