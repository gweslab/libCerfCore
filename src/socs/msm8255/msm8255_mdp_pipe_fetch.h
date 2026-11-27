#pragma once

#include "msm8255_mdp_pixel.h"
#include "../../lcd/plane_fetch.h"
#include "../../lcd/qseed_scaler.h"

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

class Msm8255MdpPipeFetch : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    struct Rect {
        uint32_t x, y, w, h;
    };

    Rect Fetch(const Msm8255MdpRegisterRead& reg, uint32_t pipe,
               std::vector<Msm8255MdpSample>& out);

private:
    PlaneLayout ReadLayout(uint32_t format, uint32_t unpack, uint32_t pipe);
    static QseedScaler::Taps ReadTaps(const Msm8255MdpRegisterRead& reg, uint32_t pipe_base, uint32_t select);
    void ScaleVg(const Msm8255MdpRegisterRead& reg, uint32_t pipe, uint32_t op_mode,
                 uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                 std::vector<Msm8255MdpSample>& out);
    void FetchSource(const Msm8255MdpRegisterRead& reg, uint32_t pipe, const PlaneLayout& layout,
                     uint32_t w, uint32_t h);
    void Scale(const Msm8255MdpRegisterRead& reg, uint32_t pipe, uint32_t op_mode,
               uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
               std::vector<Msm8255MdpSample>& out);

    std::vector<Msm8255MdpSample> source_;
    std::vector<Msm8255MdpSample> columns_;
    PlaneFetch::Scratch           scratch_;
};

}
