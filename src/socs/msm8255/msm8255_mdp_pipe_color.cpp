#include "msm8255_mdp_pipe_color.h"

#include "msm8255_id.h"
#include "msm8255_mdp_regs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>

#include <algorithm>

namespace cerfcore {

namespace {

using namespace msm8255_mdp;

constexpr uint32_t kOpSrcYcbcr  = 1u << 9;
constexpr uint32_t kOpDstYcbcr  = 1u << 10;
constexpr uint32_t kOpCscEnable = 1u << 11;
constexpr uint32_t kOpIgcEnable = 1u << 16;

constexpr uint32_t kCscCoeffBits  = 13u;
constexpr uint32_t kCscBiasBits   = 9u;
constexpr uint32_t kCscClampMask  = 0xFFu;

constexpr uint32_t kIgcEntries  = 256u;
constexpr uint32_t kIgcValueMask = 0xFFFu;
constexpr uint32_t kIgcC1Shift   = 16u;

int32_t SignExtend(uint32_t value, uint32_t bits) {
    const uint32_t sign  = 1u << (bits - 1u);
    const uint32_t field = value & ((1u << bits) - 1u);
    return static_cast<int32_t>(field ^ sign) - static_cast<int32_t>(sign);
}


}

bool Msm8255MdpPipeColor::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

Msm8255MdpPipeColor::Path Msm8255MdpPipeColor::Read(const Msm8255MdpRegisterRead& reg,
                                                    uint32_t pipe, uint32_t op_mode) {
    auto& fatal = emu_.Get<Fatal>();
    const uint32_t b = kPipeBase + pipe * kPipeStride;
    Path path{};
    path.csc      = (op_mode & kOpCscEnable) != 0u;
    path.ycbcr_in = (op_mode & kOpSrcYcbcr) != 0u;
    path.igc      = (op_mode & kOpIgcEnable) != 0u;
    if ((op_mode & kOpDstYcbcr) != 0u || (path.ycbcr_in && !path.csc) ||
        (path.csc && pipe >= kVgPipeCount)) {
        fatal.Die("msm8255 mdp pipe color: pipe %u op mode %08X has no RGB output path", pipe,
                  op_mode);
    }
    if (path.csc) {
        for (uint32_t i = 0; i < path.matrix.size(); ++i) {
            path.matrix[i] = SignExtend(reg(b + kPipeCsc + 4u * i), kCscCoeffBits);
        }
        for (uint32_t i = 0; i < path.pre_bias.size(); ++i) {
            path.pre_bias[i]  = SignExtend(reg(b + kPipeCscPreBias + 4u * i), kCscBiasBits);
            path.post_bias[i] = SignExtend(reg(b + kPipeCscPostBias + 4u * i), kCscBiasBits);
        }
        for (uint32_t i = 0; i < path.pre_clamp.size(); ++i) {
            path.pre_clamp[i]  = static_cast<int32_t>(reg(b + kPipeCscPreClamp + 4u * i) & kCscClampMask);
            path.post_clamp[i] = static_cast<int32_t>(reg(b + kPipeCscPostClamp + 4u * i) & kCscClampMask);
        }
        for (uint32_t i = 0; i < path.pre_clamp.size(); i += 2u) {
            if (path.pre_clamp[i] > path.pre_clamp[i + 1u] ||
                path.post_clamp[i] > path.post_clamp[i + 1u]) {
                fatal.Die("msm8255 mdp pipe color: pipe %u csc component %u clamps %d..%d and "
                          "%d..%d", pipe, i / 2u, path.pre_clamp[i], path.pre_clamp[i + 1u],
                          path.post_clamp[i], path.post_clamp[i + 1u]);
            }
        }
    }
    if (path.igc) {
        for (uint32_t i = 0; i < kIgcEntries; ++i) {
            const uint32_t c01 = reg(b + kPipeIgcC01 + 4u * i);
            path.igc_table[0][i] = static_cast<uint16_t>(c01 & kIgcValueMask);
            path.igc_table[1][i] = static_cast<uint16_t>((c01 >> kIgcC1Shift) & kIgcValueMask);
            path.igc_table[2][i] = static_cast<uint16_t>(reg(b + kPipeIgcC2 + 4u * i) & kIgcValueMask);
        }
    }
    return path;
}

Msm8255MdpPixel Msm8255MdpPipeColor::Apply(const Path& path, const Msm8255MdpSample& s) {
    std::array<uint32_t, 3> rgb = {s.c2, s.c0, s.c1};
    if (path.csc) {
        rgb = CscConvert(path, path.ycbcr_in ? std::array<int32_t, 3>{s.c0, s.c1, s.c2}
                                             : std::array<int32_t, 3>{s.c2, s.c0, s.c1});
    }
    if (!path.igc) {
        return Msm8255MdpPixel{static_cast<uint16_t>(lcd_pixel::Expand8To12(rgb[0])),
                               static_cast<uint16_t>(lcd_pixel::Expand8To12(rgb[1])),
                               static_cast<uint16_t>(lcd_pixel::Expand8To12(rgb[2])), s.a};
    }
    return Msm8255MdpPixel{path.igc_table[2][rgb[0]], path.igc_table[0][rgb[1]],
                           path.igc_table[1][rgb[2]], s.a};
}

REGISTER_SERVICE(Msm8255MdpPipeColor);

}
