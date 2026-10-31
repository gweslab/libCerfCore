#pragma once

#include "cerf_virt_blt_descriptor.h"

namespace cerfcore {
namespace CerfVirt {

struct CerfRotatedLayout {
    int32_t origin;
    int32_t x_pitch;
    int32_t y_pitch;
};

inline CerfRotatedLayout CerfRotatedLayoutFor(uint32_t rotate, int32_t screen_w, int32_t screen_h,
                                              int32_t stride, int32_t bytes_px) {
    const int32_t     last_x = screen_w - 1;
    const int32_t     last_y = screen_h - 1;
    CerfRotatedLayout l;
    if (rotate == kCerfRotate90) {
        l.origin  = last_y * stride;
        l.x_pitch = -stride;
        l.y_pitch = bytes_px;
    } else if (rotate == kCerfRotate180) {
        l.origin  = last_y * stride + last_x * bytes_px;
        l.x_pitch = -bytes_px;
        l.y_pitch = -stride;
    } else if (rotate == kCerfRotate270) {
        l.origin  = last_x * bytes_px;
        l.x_pitch = stride;
        l.y_pitch = -bytes_px;
    } else {
        l.origin  = 0;
        l.x_pitch = bytes_px;
        l.y_pitch = stride;
    }
    return l;
}

inline int32_t CerfRotatedOffset(const CerfRotatedLayout& l, int32_t x, int32_t y) {
    return l.origin + x * l.x_pitch + y * l.y_pitch;
}

}
}
