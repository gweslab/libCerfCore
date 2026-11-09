#pragma once

#include "nec_vrc4171a_lcd.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class NecVrc4171aLcdScanout {
public:
    explicit NecVrc4171aLcdScanout(const uint8_t* frame_memory) : frame_memory_(frame_memory) {}

    void DecodeLine(const NecVrc4171aLcdScan& scan, uint32_t line, uint32_t count, uint32_t* rgb666);

private:
    uint32_t CursorOverlay(const NecVrc4171aLcdScan& scan, uint32_t line, uint32_t x, uint32_t value,
                           bool direct, bool* replaced) const;
    uint32_t CursorColor(const NecVrc4171aLcdScan& scan, bool color1, bool direct) const;
    static uint32_t DirectColor(const NecVrc4171aLcdScan& scan, uint32_t value);

    const uint8_t*       frame_memory_;
    std::vector<uint8_t> line_bytes_;
};

}
