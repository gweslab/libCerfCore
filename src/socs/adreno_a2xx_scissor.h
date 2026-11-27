#pragma once
#include <cstdint>

namespace cerfcore {

class AdrenoA2xxScissor {
public:
    AdrenoA2xxScissor(uint32_t window_offset, uint32_t window_tl, uint32_t window_br,
                      uint32_t screen_tl, uint32_t screen_br, bool offset_enabled);

    static int X(uint32_t value);
    static int Y(uint32_t value);

    int OffsetX() const { return offset_x_; }
    int OffsetY() const { return offset_y_; }
    int Left() const { return left_; }
    int Top() const { return top_; }
    int Right() const { return right_; }
    int Bottom() const { return bottom_; }

private:
    int offset_x_;
    int offset_y_;
    int left_;
    int top_;
    int right_;
    int bottom_;
};

}
