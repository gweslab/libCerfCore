#pragma once

#include "msm8255_mdp_pixel.h"
#include "../../lcd/csc_matrix.h"

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class Msm8255MdpPipeColor : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    struct Path : CscMatrix {
        bool csc      = false;
        bool ycbcr_in = false;
        bool igc      = false;
        std::array<std::array<uint16_t, 256>, 3> igc_table{};
    };

    Path Read(const Msm8255MdpRegisterRead& reg, uint32_t pipe, uint32_t op_mode);
    static Msm8255MdpPixel Apply(const Path& path, const Msm8255MdpSample& s);
};

}
