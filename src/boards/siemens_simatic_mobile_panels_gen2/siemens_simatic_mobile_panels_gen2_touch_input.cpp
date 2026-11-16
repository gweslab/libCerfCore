#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mobile_panels_gen2_board_profile.h"
#include <cerfcore/core/board_database.h>
#include "siemens_simatic_mobile_panels_gen2_touch_calibration.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/touch_input.h>
#include "../../peripherals/ti_tsc2017/tsc2017_host_state.h"

#include <algorithm>
#include <mutex>
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

uint16_t Clamp12(double v) {
    if (v < 0.0) v = 0.0;
    const uint32_t iv = static_cast<uint32_t>(v + 0.5);
    return static_cast<uint16_t>(std::min<uint32_t>(iv, 0x0FFFu));
}

struct Point {
    double x, y;
};

double Magnitude(double value) { return value < 0.0 ? -value : value; }

void Crosshairs(uint32_t w, uint32_t h, Point out[5]) {
    const double dx = static_cast<double>(2u * (w / 10u));
    const double dy = static_cast<double>(2u * (h / 10u));
    out[0] = {static_cast<double>(w / 2u), static_cast<double>(h / 2u)};
    out[1] = {dx, dy};
    out[2] = {dx, static_cast<double>(h) - dy};
    out[3] = {static_cast<double>(w) - dx, static_cast<double>(h) - dy};
    out[4] = {static_cast<double>(w) - dx, dy};
}

bool Solve(const Point screen[5], const double raw[5], double out[3]) {
    double m[3][4] = {};
    for (uint32_t i = 0; i < 5u; ++i) {
        const double v[3] = {screen[i].x, screen[i].y, 1.0};
        for (uint32_t r = 0; r < 3u; ++r) {
            for (uint32_t c = 0; c < 3u; ++c)
                m[r][c] += v[r] * v[c];
            m[r][3] += v[r] * raw[i];
        }
    }
    for (uint32_t i = 0; i < 3u; ++i) {
        uint32_t piv = i;
        for (uint32_t r = i + 1u; r < 3u; ++r)
            if (Magnitude(m[r][i]) > Magnitude(m[piv][i])) piv = r;
        if (Magnitude(m[piv][i]) < 1e-9) return false;
        for (uint32_t c = 0; c < 4u; ++c) {
            const double t = m[i][c];
            m[i][c] = m[piv][c];
            m[piv][c] = t;
        }
        for (uint32_t r = i + 1u; r < 3u; ++r) {
            const double f = m[r][i] / m[i][i];
            for (uint32_t c = i; c < 4u; ++c)
                m[r][c] -= f * m[i][c];
        }
    }
    for (int i = 2; i >= 0; --i) {
        double s = m[i][3];
        for (uint32_t c = static_cast<uint32_t>(i) + 1u; c < 3u; ++c)
            s -= m[i][c] * out[c];
        out[i] = s / m[i][i];
    }
    return true;
}

struct TouchCalibrationMap {
    double ax = 0.0, bx = 0.0, cx = 0.0;
    double ay = 0.0, by = 0.0, cy = 0.0;
};

bool BuildMap(const SiemensSimaticMobilePanelsGen2TouchCalibrationPoints& points,
              uint32_t width, uint32_t height, TouchCalibrationMap& map) {
    Point screen[5];
    Crosshairs(width, height, screen);
    double raw_x[5], raw_y[5];
    for (uint32_t i = 0; i < 5u; ++i) {
        raw_x[i] = points.x[i];
        raw_y[i] = points.y[i];
    }
    double sx[3] = {}, sy[3] = {};
    if (!Solve(screen, raw_x, sx) || !Solve(screen, raw_y, sy)) return false;
    map = {sx[0], sx[1], sx[2], sy[0], sy[1], sy[2]};
    return true;
}

class SiemensSimaticMobilePanelsGen2TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }

    void OnReady() override {
        const auto& profile = SiemensSimaticMobilePanelsGen2BoardProfileFor(emu_.Get<BoardContext>().GetBoardId());
        panel_ = ResolveSiemensSimaticMobilePanelsGen2Panel(emu_.Get<BoardDatabase>().FindBoard(profile.board), profile);
        size_suffix_ = profile.touch_size_suffix;
        last_x_ = static_cast<int>(panel_.width / 2u);
        last_y_ = static_cast<int>(panel_.height / 2u);
        ReadMap();
        emu_.Get<Tsc2017HostState>().SetPen(false, kInitialPenXStub, kInitialPenYStub);
    }

protected:
    void PenDown(int x, int y) override { Apply(true, x, y); }
    void PenMove(int x, int y) override { Apply(true, x, y); }
    void PenUp(int x, int y) override { Apply(false, x, y); }
    void CaptureLost() override { Apply(false, last_x_, last_y_); }

private:
    void ReadMap() {
        const auto* row = emu_.Get<SiemensSimaticMobilePanelsGen2TouchCalibration>().RowsFor(size_suffix_);
        if (!row)
            emu_.Get<Fatal>().Die("Siemens Mobile Panels: touch calibration table has no row for %s", size_suffix_);
        TouchCalibrationMap map;
        if (!BuildMap(row->points, panel_.width, panel_.height, map))
            emu_.Get<Fatal>().Die("Siemens Mobile Panels: touch calibration geometry is singular for %s", size_suffix_);
        std::lock_guard<std::mutex> lk(map_mutex_);
        map_ = map;
    }

    void Apply(bool down, int x, int y) {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= static_cast<int>(panel_.width)) x = static_cast<int>(panel_.width - 1u);
        if (y >= static_cast<int>(panel_.height)) y = static_cast<int>(panel_.height - 1u);
        last_x_ = x;
        last_y_ = y;
        std::lock_guard<std::mutex> lk(map_mutex_);
        const double sx = static_cast<double>(x);
        const double sy = static_cast<double>(y);
        emu_.Get<Tsc2017HostState>().SetPen(
            down, Clamp12(map_.ax * sx + map_.bx * sy + map_.cx),
            Clamp12(map_.ay * sx + map_.by * sy + map_.cy));
    }

    SiemensSimaticMobilePanelsGen2Panel panel_{};
    TouchCalibrationMap map_{};
    std::mutex map_mutex_;
    const char* size_suffix_ = "";
    int last_x_ = 0;
    int last_y_ = 0;
};

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2TouchInput, TouchInput);

}
