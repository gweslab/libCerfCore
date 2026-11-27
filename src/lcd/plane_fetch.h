#pragma once

#include "plane_sample.h"

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

struct PlaneSource {
    PlaneLayout             layout;
    std::array<uint32_t, 3> base{};
    std::array<uint32_t, 3> stride{};
    uint32_t                x       = 0;
    uint32_t                y       = 0;
    uint32_t                w       = 0;
    uint32_t                h       = 0;
    uint32_t                frame_w = 0;
    uint32_t                frame_h = 0;
};

using PlaneMemoryRead = std::function<void(uint32_t address, uint8_t* dst, uint32_t bytes)>;

class PlaneFetch : public Service {
public:
    using Service::Service;

    struct Scratch {
        std::vector<std::vector<uint8_t>> band_rows;
    };

    void Fetch(const PlaneSource& src, const PlaneMemoryRead& read, Scratch& scratch,
               std::vector<PlaneSample>& out);

    void ResampleBilinear(const std::vector<PlaneSample>& src, uint32_t src_w, uint32_t src_h,
                          uint32_t dst_w, uint32_t dst_h, uint32_t step_x, uint32_t step_y, bool lerp,
                          bool flip_x, bool flip_y, std::vector<PlaneSample>& out);

    static PlaneSample Unpack(const uint8_t* src, const PlaneLayout& l, uint32_t phase);
};

}
