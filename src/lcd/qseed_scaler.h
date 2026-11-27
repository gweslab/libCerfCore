#pragma once

#include "plane_sample.h"

#include <cerfcore/core/service.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class QseedScaler : public Service {
public:
    using Service::Service;

    static constexpr uint32_t kPhases          = 32u;
    static constexpr uint32_t kSetWords        = 64u;
    static constexpr uint32_t kSetMask         = 0xFu;
    static constexpr uint32_t kUnitFir         = 0u;
    static constexpr uint32_t kUnitMn          = 1u;
    static constexpr uint32_t kUnitPixelRepeat = 2u;

    using Taps            = std::array<std::array<int32_t, 4>, kPhases>;
    using CoefficientRead = std::function<uint32_t(uint32_t word)>;

    struct Axis {
        uint32_t unit;
        uint32_t step;
        bool     flip;
        Taps     taps;
    };

    static Taps SelectTaps(const CoefficientRead& coefficient, uint32_t select);
    void Scale(const std::vector<PlaneSample>& src, uint32_t src_w, uint32_t src_h, const Axis& ax,
               const Axis& ay, uint32_t dst_w, uint32_t dst_h, std::vector<PlaneSample>& columns,
               std::vector<PlaneSample>& out);

private:
    static void Resample(const PlaneSample* src, size_t src_stride, uint32_t src_n, PlaneSample* dst,
                         size_t dst_stride, uint32_t dst_n, const Axis& axis);
};

}
