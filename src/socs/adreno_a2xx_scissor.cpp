#include "adreno_a2xx_scissor.h"
#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint32_t kCoordinateMask        = 0x7FFFu;
constexpr uint32_t kWindowOffsetDisable   = 0x80000000u;

int Signed15(uint32_t value) {
    return static_cast<int>((value & kCoordinateMask) ^ 0x4000u) - 0x4000;
}

}

int AdrenoA2xxScissor::X(uint32_t value) { return int(value & kCoordinateMask); }

int AdrenoA2xxScissor::Y(uint32_t value) { return int((value >> 16) & kCoordinateMask); }

AdrenoA2xxScissor::AdrenoA2xxScissor(uint32_t window_offset, uint32_t window_tl, uint32_t window_br,
                                     uint32_t screen_tl, uint32_t screen_br, bool offset_enabled)
    : offset_x_(offset_enabled ? Signed15(window_offset) : 0),
      offset_y_(offset_enabled ? Signed15(window_offset >> 16) : 0) {
    const bool window_offset_applies = (window_tl & kWindowOffsetDisable) == 0u;
    const int wx = window_offset_applies ? offset_x_ : 0;
    const int wy = window_offset_applies ? offset_y_ : 0;
    left_   = (std::max)(X(screen_tl), X(window_tl) + wx);
    top_    = (std::max)(Y(screen_tl), Y(window_tl) + wy);
    right_  = (std::min)(X(screen_br), X(window_br) + wx);
    bottom_ = (std::min)(Y(screen_br), Y(window_br) + wy);
}

}
