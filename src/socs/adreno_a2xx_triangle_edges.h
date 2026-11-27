#pragma once
#include <array>

namespace cerfcore {

class AdrenoA2xxTriangleEdges {
public:
    struct Vertex {
        double x;
        double y;
    };

    AdrenoA2xxTriangleEdges(const std::array<Vertex, 3>& points, double sign, double area, bool weights)
        : points_(points), sign_(sign), area_(area), weights_(weights),
          top_left_{TopLeft(points[1], points[2]), TopLeft(points[2], points[0]), TopLeft(points[0], points[1])} {}

    static double Edge(const Vertex& a, const Vertex& b, double x, double y) {
        return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
    }

    bool Covers(int x, int y, std::array<double, 3>& weights) const {
        const double a = Edge(points_[1],points_[2],x+0.5,y+0.5)*sign_;
        const double b = Edge(points_[2],points_[0],x+0.5,y+0.5)*sign_;
        const double c = Edge(points_[0],points_[1],x+0.5,y+0.5)*sign_;
        if (weights_) weights = {a/area_,b/area_,c/area_};
        return !(a < 0 || b < 0 || c < 0 || (a == 0 && !top_left_[0]) || (b == 0 && !top_left_[1]) ||
            (c == 0 && !top_left_[2]));
    }

    void RowSpan(int y, int left, int right, int& xl, int& xr) const {
        xl = left;
        xr = right;
        for (unsigned e = 0; e < 3u && xl < xr; ++e) {
            const double slope = -(points_[kEdgePoints[e][1]].y - points_[kEdgePoints[e][0]].y) * sign_;
            if (slope == 0.0) {
                if (!EdgePasses(e,xl,y)) xr = xl;
            } else if (slope > 0.0) {
                if (!EdgePasses(e,xr-1,y)) { xr = xl; continue; }
                int lo = xl, hi = xr - 1;
                while (lo < hi) {
                    const int mid = lo + (hi - lo) / 2;
                    if (EdgePasses(e,mid,y)) hi = mid; else lo = mid + 1;
                }
                xl = lo;
            } else {
                if (!EdgePasses(e,xl,y)) { xr = xl; continue; }
                int lo = xl, hi = xr - 1;
                while (lo < hi) {
                    const int mid = lo + (hi - lo + 1) / 2;
                    if (EdgePasses(e,mid,y)) lo = mid; else hi = mid - 1;
                }
                xr = lo + 1;
            }
        }
    }

private:
    static constexpr std::array<std::array<unsigned, 2>, 3> kEdgePoints{{{1u,2u},{2u,0u},{0u,1u}}};

    bool TopLeft(const Vertex& a, const Vertex& b) const {
        const double dx = (b.x-a.x)*sign_;
        const double dy = (b.y-a.y)*sign_;
        return dy < 0.0 || (dy == 0.0 && dx > 0.0);
    }

    bool EdgePasses(unsigned e, int x, int y) const {
        const double value = Edge(points_[kEdgePoints[e][0]],points_[kEdgePoints[e][1]],x+0.5,y+0.5)*sign_;
        return !(value < 0 || (value == 0 && !top_left_[e]));
    }

    std::array<Vertex, 3> points_;
    double                sign_;
    double                area_;
    bool                  weights_;
    std::array<bool, 3>   top_left_;
};

}
