#include "qseed_scaler.h"

#include <cerfcore/core/cerf_emulator.h>

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(QseedScaler);

namespace {

constexpr uint32_t kPhaseIndexShift = kPlanePhaseBits - 5u;
constexpr uint32_t kTapFractionBits = 9u;

int32_t Tap(uint32_t field) {
    return static_cast<int32_t>((field & 0xFFFu) ^ 0x800u) - 0x800;
}

uint8_t WeighComponent(const std::array<const PlaneSample*, 4>& p, const std::array<int32_t, 4>& t,
                       uint8_t PlaneSample::* c) {
    int32_t sum = 1 << (kTapFractionBits - 1u);
    for (uint32_t k = 0; k < 4u; ++k) sum += t[k] * int32_t(p[k]->*c);
    return static_cast<uint8_t>(std::clamp(sum >> kTapFractionBits, 0, 255));
}

uint8_t AverageComponent(const PlaneSample* src, size_t stride, uint32_t first, uint32_t end, bool flip,
                         uint32_t n, uint8_t PlaneSample::* c) {
    uint32_t sum = 0u;
    for (uint32_t k = first; k < end; ++k) sum += src[size_t(flip ? n - 1u - k : k) * stride].*c;
    const uint32_t count = end - first;
    return static_cast<uint8_t>((sum + count / 2u) / count);
}

}

QseedScaler::Taps QseedScaler::SelectTaps(const CoefficientRead& coefficient, uint32_t select) {
    const uint32_t set = (select & kSetMask) * kSetWords;
    Taps taps{};
    for (uint32_t p = 0; p < kPhases; ++p) {
        const uint32_t w0 = coefficient(set + 2u * p);
        const uint32_t w1 = coefficient(set + 2u * p + 1u);
        taps[p] = {Tap(w0), Tap(w0 >> 16), Tap(w1), Tap(w1 >> 16)};
    }
    return taps;
}

void QseedScaler::Resample(const PlaneSample* src, size_t src_stride, uint32_t src_n, PlaneSample* dst,
                           size_t dst_stride, uint32_t dst_n, const Axis& axis) {
    const auto at = [&](int64_t k) {
        const uint32_t c = static_cast<uint32_t>(std::clamp<int64_t>(k, 0, int64_t(src_n) - 1));
        return src + size_t(axis.flip ? src_n - 1u - c : c) * src_stride;
    };
    for (uint32_t i = 0; i < dst_n; ++i) {
        const uint64_t pos  = uint64_t(i) * axis.step;
        const uint32_t base = std::min<uint32_t>(static_cast<uint32_t>(pos >> kPlanePhaseBits), src_n - 1u);
        PlaneSample&   d    = dst[size_t(i) * dst_stride];
        if (axis.unit == kUnitPixelRepeat) {
            d = *at(base);
        } else if (axis.unit == kUnitFir) {
            const std::array<const PlaneSample*, 4> p = {at(int64_t(base) - 1), at(base), at(int64_t(base) + 1),
                                                         at(int64_t(base) + 2)};
            const auto& t = axis.taps[(pos >> kPhaseIndexShift) & (kPhases - 1u)];
            d = PlaneSample{WeighComponent(p, t, &PlaneSample::c0), WeighComponent(p, t, &PlaneSample::c1),
                            WeighComponent(p, t, &PlaneSample::c2), WeighComponent(p, t, &PlaneSample::a)};
        } else {
            const uint32_t next = static_cast<uint32_t>((uint64_t(i) + 1u) * axis.step >> kPlanePhaseBits);
            const uint32_t end  = std::max(base + 1u, std::min(next, src_n));
            d = PlaneSample{AverageComponent(src, src_stride, base, end, axis.flip, src_n, &PlaneSample::c0),
                            AverageComponent(src, src_stride, base, end, axis.flip, src_n, &PlaneSample::c1),
                            AverageComponent(src, src_stride, base, end, axis.flip, src_n, &PlaneSample::c2),
                            AverageComponent(src, src_stride, base, end, axis.flip, src_n, &PlaneSample::a)};
        }
    }
}

void QseedScaler::Scale(const std::vector<PlaneSample>& src, uint32_t src_w, uint32_t src_h, const Axis& ax,
                        const Axis& ay, uint32_t dst_w, uint32_t dst_h, std::vector<PlaneSample>& columns,
                        std::vector<PlaneSample>& out) {
    columns.resize(size_t(src_h) * dst_w);
    for (uint32_t y = 0; y < src_h; ++y)
        Resample(src.data() + size_t(y) * src_w, 1u, src_w, columns.data() + size_t(y) * dst_w, 1u, dst_w, ax);
    out.resize(size_t(dst_w) * dst_h);
    for (uint32_t x = 0; x < dst_w; ++x)
        Resample(columns.data() + x, dst_w, src_h, out.data() + x, dst_w, dst_h, ay);
}

}
