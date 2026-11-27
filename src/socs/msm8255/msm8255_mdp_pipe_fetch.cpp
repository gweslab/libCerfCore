#include "msm8255_mdp_pipe_fetch.h"

#include "msm8255_id.h"
#include "msm8255_mdp_regs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <algorithm>

namespace cerfcore {

namespace {

using namespace msm8255_mdp;

constexpr uint32_t kFormatAlphaEnable  = 1u << 8;
constexpr uint32_t kFormatCppShift     = 9u;
constexpr uint32_t kFormatRotated90    = 1u << 12;
constexpr uint32_t kFormatUnpackShift  = 13u;
constexpr uint32_t kFormatTight        = 1u << 17;
constexpr uint32_t kFormatAlignMsb     = 1u << 18;
constexpr uint32_t kFormatPlanesShift  = 19u;
constexpr uint32_t kFormatSolidFill    = 1u << 22;
constexpr uint32_t kFormatChromaShift  = 26u;
constexpr uint32_t kFormatChromaSite   = 1u << 28;
constexpr uint32_t kFormatFrameShift   = 29u;
constexpr uint32_t kFormatModeled = 0xFFu | kFormatAlphaEnable | (3u << kFormatCppShift) | kFormatRotated90 |
                                    (3u << kFormatUnpackShift) | kFormatTight | kFormatAlignMsb |
                                    (3u << kFormatPlanesShift) | kFormatSolidFill |
                                    (3u << kFormatChromaShift) | kFormatChromaSite |
                                    (3u << kFormatFrameShift);
constexpr uint32_t kFrameLinear      = 0u;
constexpr uint32_t kFrameTileYcbcr420 = 2u;
constexpr uint32_t kChroma420        = 3u;

constexpr uint32_t kOpScaleX      = 1u << 0;
constexpr uint32_t kOpScaleY      = 1u << 1;
constexpr uint32_t kOpScaleXShift = 2u;
constexpr uint32_t kOpScaleYShift = 4u;
constexpr uint32_t kOpRgbBilinear = 1u << 3;
constexpr uint32_t kOpFlipLr      = 1u << 13;
constexpr uint32_t kOpFlipUd      = 1u << 14;
constexpr uint32_t kOpModeled     = kOpScaleX | kOpScaleY | (0xFu << kOpScaleXShift) |
                                    (1u << 9) | (1u << 10) | (1u << 11) | kOpFlipLr | kOpFlipUd |
                                    (1u << 15) | (1u << 16) | (1u << 17) | (1u << 18) | (1u << 19);
constexpr uint32_t kLumaPerUnit = 2u;

}

bool Msm8255MdpPipeFetch::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

PlaneLayout Msm8255MdpPipeFetch::ReadLayout(uint32_t format, uint32_t unpack, uint32_t pipe) {
    auto& fatal = emu_.Get<Fatal>();
    PlaneLayout l{};
    l.planes   = (format >> kFormatPlanesShift) & 3u;
    const uint32_t chroma = (format >> kFormatChromaShift) & 3u;
    l.chroma_h = (chroma == 1u || chroma == 3u) ? 1u : 0u;
    l.chroma_v = (chroma == 2u || chroma == 3u) ? 1u : 0u;
    const uint32_t cpp = ((format >> kFormatCppShift) & 3u) + 1u;
    l.elements = ((format >> kFormatUnpackShift) & 3u) + 1u;
    l.alpha    = (format & kFormatAlphaEnable) != 0u;
    const uint32_t frame = (format >> kFormatFrameShift) & 3u;
    l.tiled    = frame == kFrameTileYcbcr420;
    if ((format & ~kFormatModeled) != 0u || l.planes == 3u ||
        (frame != kFrameLinear && (!l.tiled || l.planes != kPlanesPseudoPlanar || chroma != kChroma420)) ||
        (l.planes == kPlanesInterleaved && l.chroma_v != 0u) ||
        (l.planes != kPlanesInterleaved && pipe >= kVgPipeCount)) {
        fatal.Die("msm8255 mdp pipe fetch: pipe %u format %08X is not modeled", pipe, format);
    }
    const bool shared_chroma = l.planes == kPlanesInterleaved && l.chroma_h != 0u;
    l.unit = cpp << (shared_chroma ? 1u : 0u);
    uint32_t luma_seen = 0u;
    const std::array<uint32_t, 4> bits = {kBpcBits[format & 3u], kBpcBits[(format >> 2) & 3u],
                                          kBpcBits[(format >> 4) & 3u],
                                          kBpcAlphaBits[(format >> 6) & 3u]};
    const bool tight = (format & kFormatTight) != 0u;
    uint32_t shift = 0u;
    for (uint32_t i = 0; i < l.elements; ++i) {
        const uint32_t c = (unpack >> (8u * i)) & 0xFFu;
        if (c > kPlaneComponentAlpha) {
            fatal.Die("msm8255 mdp pipe fetch: pipe %u unpack pattern %08X", pipe, unpack);
        }
        l.component[i] = c;
        l.bits[i]      = bits[c];
        l.pixel[i]     = shared_chroma && c == kPlaneComponentLuma ? luma_seen++ : kPlaneEveryPixel;
        if (tight) {
            l.shift[i] = shift;
            shift += bits[c];
        } else {
            l.shift[i] = 8u * i + ((format & kFormatAlignMsb) != 0u ? 8u - bits[c] : 0u);
            shift += 8u;
        }
    }
    const uint32_t packed_bytes = l.planes == kPlanesInterleaved ? l.unit : l.elements;
    if ((shared_chroma && luma_seen != kLumaPerUnit) ||
        (l.planes == kPlanesInterleaved ? shift != l.unit * 8u : shift > packed_bytes * 8u)) {
        fatal.Die("msm8255 mdp pipe fetch: pipe %u format %08X unpack %08X packs %u bits into %u "
                  "bytes", pipe, format, unpack, shift, packed_bytes);
    }
    return l;
}

Msm8255MdpPipeFetch::Rect Msm8255MdpPipeFetch::Fetch(const Msm8255MdpRegisterRead& reg,
                                                     uint32_t pipe,
                                                     std::vector<Msm8255MdpSample>& out) {
    auto& fatal = emu_.Get<Fatal>();
    const uint32_t b        = kPipeBase + pipe * kPipeStride;
    const uint32_t src_size = reg(b + kPipeSrcSize), dst_size = reg(b + kPipeDstSize);
    const uint32_t op_mode  = reg(b + kPipeOpMode);
    const uint32_t format   = reg(b + kPipeFormat);
    const PlaneLayout layout = ReadLayout(format, reg(b + kPipeUnpack), pipe);
    if ((op_mode & ~kOpModeled) != 0u) {
        fatal.Die("msm8255 mdp pipe fetch: pipe %u op mode %08X is not modeled", pipe, op_mode);
    }
    const uint32_t src_w = src_size & 0xFFFFu, src_h = src_size >> 16;
    const Rect dst{reg(b + kPipeDstXy) & 0xFFFFu, reg(b + kPipeDstXy) >> 16, dst_size & 0xFFFFu,
                   dst_size >> 16};
    if (src_w == 0u || src_h == 0u || dst.w == 0u || dst.h == 0u) {
        fatal.Die("msm8255 mdp pipe fetch: pipe %u source size %08X destination size %08X", pipe,
                  src_size, dst_size);
    }
    if ((format & kFormatSolidFill) != 0u) {
        if (layout.planes != kPlanesInterleaved) {
            fatal.Die("msm8255 mdp pipe fetch: pipe %u solid fill in format %08X", pipe, format);
        }
        uint8_t color[4];
        cerf::le::Put32(color, reg(b + kPipeSolidColor));
        out.assign(size_t(dst.w) * dst.h, PlaneFetch::Unpack(color, layout, 0u));
        return dst;
    }
    FetchSource(reg, pipe, layout, src_w, src_h);
    Scale(reg, pipe, op_mode, src_w, src_h, dst.w, dst.h, out);
    return dst;
}

void Msm8255MdpPipeFetch::FetchSource(const Msm8255MdpRegisterRead& reg, uint32_t pipe,
                                      const PlaneLayout& l, uint32_t w, uint32_t h) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t b = kPipeBase + pipe * kPipeStride;
    const uint32_t stride_a = reg(b + kPipeStrideA), stride_b = reg(b + kPipeStrideB);
    const uint32_t frame_size = reg(b + kPipeFrameSize);
    PlaneSource src;
    src.layout  = l;
    src.base    = {reg(b + kPipeSrcp0), reg(b + kPipeSrcp1), reg(b + kPipeSrcp2)};
    src.stride  = {stride_a & 0xFFFFu, stride_a >> 16, stride_b & 0xFFFFu};
    src.x       = reg(b + kPipeSrcXy) & 0xFFFFu;
    src.y       = reg(b + kPipeSrcXy) >> 16;
    src.w       = w;
    src.h       = h;
    src.frame_w = frame_size & 0xFFFFu;
    src.frame_h = frame_size >> 16;
    emu_.Get<PlaneFetch>().Fetch(
        src, [&mem](uint32_t address, uint8_t* dst, uint32_t bytes) { mem.CopyOut(address, dst, bytes); },
        scratch_, source_);
}

void Msm8255MdpPipeFetch::Scale(const Msm8255MdpRegisterRead& reg, uint32_t pipe,
                                uint32_t op_mode, uint32_t src_w, uint32_t src_h, uint32_t dst_w,
                                uint32_t dst_h, std::vector<Msm8255MdpSample>& out) {
    const uint32_t b = kPipeBase + pipe * kPipeStride;
    const bool scale_x = (op_mode & kOpScaleX) != 0u, scale_y = (op_mode & kOpScaleY) != 0u;
    if ((!scale_x && src_w != dst_w) || (!scale_y && src_h != dst_h)) {
        emu_.Get<Fatal>().Die("msm8255 mdp pipe fetch: pipe %u resizes %ux%u to %ux%u with op mode "
                              "%08X", pipe, src_w, src_h, dst_w, dst_h, op_mode);
    }
    const uint32_t step_x = scale_x ? reg(b + kPipePhaseX) : kPlanePhaseOne;
    const uint32_t step_y = scale_y ? reg(b + kPipePhaseY) : kPlanePhaseOne;
    const bool flip_x = (op_mode & kOpFlipLr) != 0u, flip_y = (op_mode & kOpFlipUd) != 0u;
    if (step_x == kPlanePhaseOne && step_y == kPlanePhaseOne && !flip_x && !flip_y && src_w == dst_w &&
        src_h == dst_h) {
        out.swap(source_);
        return;
    }
    if (pipe < kVgPipeCount) {
        ScaleVg(reg, pipe, op_mode, src_w, src_h, dst_w, dst_h, out);
        return;
    }
    emu_.Get<PlaneFetch>().ResampleBilinear(source_, src_w, src_h, dst_w, dst_h, step_x, step_y,
                                            (op_mode & kOpRgbBilinear) != 0u, flip_x, flip_y, out);
}

QseedScaler::Taps Msm8255MdpPipeFetch::ReadTaps(const Msm8255MdpRegisterRead& reg, uint32_t pipe_base,
                                                uint32_t select) {
    return QseedScaler::SelectTaps(
        [&reg, pipe_base](uint32_t word) { return reg(pipe_base + kPipeQseedTable + 4u * word); }, select);
}

void Msm8255MdpPipeFetch::ScaleVg(const Msm8255MdpRegisterRead& reg, uint32_t pipe,
                                  uint32_t op_mode, uint32_t src_w, uint32_t src_h, uint32_t dst_w,
                                  uint32_t dst_h, std::vector<Msm8255MdpSample>& out) {
    const uint32_t b = kPipeBase + pipe * kPipeStride;
    const bool scale_x = (op_mode & kOpScaleX) != 0u, scale_y = (op_mode & kOpScaleY) != 0u;
    const uint32_t unit_y_bits = (op_mode >> kOpScaleYShift) & 3u;
    const QseedScaler::Axis ax{scale_x ? (op_mode >> kOpScaleXShift) & 3u : QseedScaler::kUnitPixelRepeat,
                               scale_x ? reg(b + kPipePhaseX) : kPlanePhaseOne, (op_mode & kOpFlipLr) != 0u,
                               ReadTaps(reg, b, reg(b + kPipeQseedSelect))};
    const QseedScaler::Axis ay{scale_y ? unit_y_bits : QseedScaler::kUnitPixelRepeat,
                               scale_y ? reg(b + kPipePhaseY) : kPlanePhaseOne, (op_mode & kOpFlipUd) != 0u,
                               ReadTaps(reg, b, reg(b + kPipeQseedSelect + 4u))};
    if (ax.unit > QseedScaler::kUnitPixelRepeat || ay.unit > QseedScaler::kUnitPixelRepeat ||
        ax.step == 0u || ay.step == 0u) {
        emu_.Get<Fatal>().Die("msm8255 mdp pipe fetch: pipe %u op mode %08X steps %08X %08X", pipe,
                              op_mode, ax.step, ay.step);
    }
    emu_.Get<QseedScaler>().Scale(source_, src_w, src_h, ax, ay, dst_w, dst_h, columns_, out);
    if ((scale_x || scale_y) && unit_y_bits != QseedScaler::kUnitPixelRepeat) {
        for (Msm8255MdpSample& s : out) s.a = kPlaneAlphaOpaque;
    }
}

REGISTER_SERVICE(Msm8255MdpPipeFetch);

}
