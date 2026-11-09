#include "nec_vrc4171a_lcd_scanout.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>

namespace cerfcore {

namespace {

constexpr uint32_t kStorageBits[8] = {1u, 2u, 4u, 8u, 8u, 8u, 16u, 16u};
constexpr uint32_t kIndexBits[8]   = {1u, 2u, 4u, 5u, 6u, 8u, 16u, 16u};

constexpr uint32_t kCursorSize          = 32u;
constexpr uint32_t kCursorLineBytes     = 8u;
constexpr uint32_t kCursorPlaneBytes    = 4u;
constexpr uint32_t kCursorPlaneBits     = 1u;
constexpr uint16_t kCursorIndexHigh     = 0x0007u;
constexpr uint32_t kCursorIndexHighShift = 5u;
constexpr uint32_t kCursorIndexLowShift = 1u;
constexpr uint16_t kCursorIndexLow      = 0x001Fu;
constexpr uint32_t kCursorRedShift      = 8u;
constexpr uint16_t kChannel             = 0x003Fu;
constexpr uint32_t kDirectMask          = 0xFFFFu;

constexpr uint32_t kBytesPerDirectPixel = 2u;
constexpr uint32_t kFive                = 0x1Fu;
constexpr uint32_t kSix                 = 0x3Fu;
constexpr uint32_t kRed565Shift         = 11u;
constexpr uint32_t kRed555Shift         = 10u;
constexpr uint32_t kGreenShift          = 5u;

}

void NecVrc4171aLcdScanout::DecodeLine(const NecVrc4171aLcdScan& scan, uint32_t line, uint32_t count,
                                       uint32_t* rgb666) {
    const uint32_t mask  = scan.memory_bytes - 1u;
    const uint32_t bits  = kStorageBits[scan.color_depth];
    const uint32_t bytes = (count * bits + 7u) / 8u;
    const uint32_t row   = scan.start + line * scan.stride;
    line_bytes_.resize(bytes);
    for (uint32_t i = 0; i < bytes; ++i) line_bytes_[i] = frame_memory_[(row + i) & mask];

    const bool direct = scan.color_depth >= NecVrc4171aLcd::kDepth555;
    for (uint32_t x = 0; x < count; ++x) {
        uint32_t value;
        if (direct)
            value = cerf::le::U16(line_bytes_.data(), x * kBytesPerDirectPixel);
        else if (bits < 8u)
            value = lcd_pixel::PackedIndexMsbFirst(line_bytes_.data(), x, bits);
        else
            value = line_bytes_[x];
        bool replaced = false;
        if (scan.cursor_on) value = CursorOverlay(scan, line, x, value, direct, &replaced);
        if (replaced)
            rgb666[x] = value;
        else
            rgb666[x] = direct ? DirectColor(scan, value) : scan.palette[value];
    }
}

uint32_t NecVrc4171aLcdScanout::CursorOverlay(const NecVrc4171aLcdScan& scan, uint32_t line, uint32_t x,
                                              uint32_t value, bool direct, bool* replaced) const {
    const int32_t cx = static_cast<int32_t>(x) - scan.cursor_left;
    const int32_t cy = static_cast<int32_t>(line) - scan.cursor_top;
    if (cx < 0 || cy < 0 || cx >= static_cast<int32_t>(kCursorSize) || cy >= static_cast<int32_t>(kCursorSize))
        return value;
    const uint8_t* plane0 = frame_memory_ + scan.cursor_base + static_cast<uint32_t>(cy) * kCursorLineBytes;
    const uint8_t* plane1 = plane0 + kCursorPlaneBytes;
    const uint32_t col    = static_cast<uint32_t>(cx);
    const bool p0 = lcd_pixel::PackedIndexMsbFirst(plane0, col, kCursorPlaneBits) != 0u;
    const bool p1 = lcd_pixel::PackedIndexMsbFirst(plane1, col, kCursorPlaneBits) != 0u;
    if (!p1) {
        *replaced = true;
        return CursorColor(scan, p0, direct);
    }
    if (!p0) return value;
    if (direct) return ~value & kDirectMask;
    return value ^ ((1u << kIndexBits[scan.color_depth]) - 1u);
}

uint32_t NecVrc4171aLcdScanout::CursorColor(const NecVrc4171aLcdScan& scan, bool color1, bool direct) const {
    const uint16_t a = color1 ? scan.cursor_color1a : scan.cursor_color0a;
    const uint16_t b = color1 ? scan.cursor_color1b : scan.cursor_color0b;
    if (direct) return NecVrc4171aLcd::PaletteEntry((a >> kCursorRedShift) & kChannel, a & kChannel, b & kChannel);
    const uint32_t index = (static_cast<uint32_t>(a & kCursorIndexHigh) << kCursorIndexHighShift) |
                           ((static_cast<uint32_t>(b) >> kCursorIndexLowShift) & kCursorIndexLow);
    return scan.palette[index];
}

uint32_t NecVrc4171aLcdScanout::DirectColor(const NecVrc4171aLcdScan& scan, uint32_t value) {
    if (scan.color_depth == NecVrc4171aLcd::kDepth565)
        return NecVrc4171aLcd::PaletteEntry(((value >> kRed565Shift) & kFive) << 1, (value >> kGreenShift) & kSix, (value & kFive) << 1);
    return NecVrc4171aLcd::PaletteEntry(((value >> kRed555Shift) & kFive) << 1, ((value >> kGreenShift) & kFive) << 1,
                  (value & kFive) << 1);
}

}
