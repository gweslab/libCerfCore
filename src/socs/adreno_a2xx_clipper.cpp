#include "adreno_a2xx_clipper.h"
#include <cmath>
#include <utility>

namespace cerfcore {

namespace {

constexpr unsigned kPlaneCount = 6u;

double PlaneSign(unsigned plane) { return (plane & 1u) ? -1.0 : 1.0; }

}

AdrenoA2xxClipper::AdrenoA2xxClipper(bool directx_z, bool window_linear_varyings)
    : directx_z_(directx_z), window_linear_varyings_(window_linear_varyings) {}

bool AdrenoA2xxClipper::Outside(const AdrenoA2xxVec4& p) const {
    const bool z_outside = directx_z_ ? (p[2] < 0.0f || p[2] > p[3]) : std::abs(p[2]) > p[3];
    return std::abs(p[0]) > p[3] || std::abs(p[1]) > p[3] || z_outside;
}

bool AdrenoA2xxClipper::ZeroPlane(unsigned plane) const {
    return directx_z_ && plane / 2u == 2u && PlaneSign(plane) > 0.0;
}

double AdrenoA2xxClipper::Distance(const AdrenoA2xxShaderState& vertex, unsigned plane) const {
    const auto& p = vertex.exports[kAdrenoA2xxPositionExport];
    if (ZeroPlane(plane)) return double(p[2]);
    return double(p[3]) + PlaneSign(plane) * p[plane / 2u];
}

std::vector<AdrenoA2xxShaderState> AdrenoA2xxClipper::Clip(
    const std::array<AdrenoA2xxShaderState, 3>& vertices) const {
    std::vector<AdrenoA2xxShaderState> polygon(vertices.begin(), vertices.end());
    std::vector<AdrenoA2xxShaderState> next;
    for (unsigned plane = 0; plane < kPlaneCount && !polygon.empty(); ++plane) {
        next.clear();
        const unsigned axis = plane / 2u;
        for (size_t i = 0; i < polygon.size(); ++i) {
            const auto& a = polygon[i];
            const auto& b = polygon[(i + 1u) % polygon.size()];
            const double da = Distance(a, plane);
            const double db = Distance(b, plane);
            if (da >= 0.0) next.push_back(a);
            if ((da < 0.0) == (db < 0.0)) continue;
            const auto& inside = da >= 0.0 ? a : b;
            const auto& out = da < 0.0 ? a : b;
            const double di = da >= 0.0 ? da : db;
            const double dout = da < 0.0 ? da : db;
            const double t = di / (di - dout);
            AdrenoA2xxShaderState intersection{};
            intersection.export_mask = inside.export_mask & out.export_mask;
            const double varying_t = window_linear_varyings_
                ? t * out.exports[kAdrenoA2xxPositionExport][3] /
                      ((1.0 - t) * inside.exports[kAdrenoA2xxPositionExport][3] + t * out.exports[kAdrenoA2xxPositionExport][3])
                : t;
            for (unsigned slot = 0; slot < intersection.exports.size(); ++slot) {
                if (!(intersection.export_mask & (uint64_t{1} << slot))) continue;
                const double fraction = slot == kAdrenoA2xxPositionExport ? t : varying_t;
                for (unsigned c = 0; c < 4u; ++c)
                    intersection.exports[slot][c] = static_cast<float>(
                        (1.0 - fraction) * inside.exports[slot][c] + fraction * out.exports[slot][c]);
            }
            intersection.exports[kAdrenoA2xxPositionExport][axis] = ZeroPlane(plane)
                ? 0.0f
                : static_cast<float>(-PlaneSign(plane) * intersection.exports[kAdrenoA2xxPositionExport][3]);
            next.push_back(std::move(intersection));
        }
        polygon.swap(next);
    }
    return polygon;
}

}
