#include "msm8255_mdp_mixer.h"

#include "msm8255_id.h"
#include "msm8255_mdp_pipe_color.h"
#include "msm8255_mdp_pipe_fetch.h"
#include "msm8255_mdp_regs.h"
#include "../../core/parallel_jobs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <algorithm>
#include <functional>

namespace cerfcore {

namespace {

using namespace msm8255_mdp;

constexpr uint32_t kMixerFieldBits = 4u;
constexpr uint32_t kMixerStageMask = 7u;
constexpr uint32_t kMixerOneBit    = 8u;
constexpr uint32_t kStageUnused    = 0u;
constexpr uint32_t kStageBase      = 1u;
constexpr uint32_t kStageFirst     = 2u;
constexpr uint32_t kStageIds       = 8u;
constexpr uint32_t kRowBandsPerLane = 4u;

constexpr uint32_t kOpFgInv    = 1u << 2;
constexpr uint32_t kOpFgMod    = 1u << 3;
constexpr uint32_t kOpBgShift  = 4u;
constexpr uint32_t kOpBgInv    = 1u << 6;
constexpr uint32_t kOpBgMod    = 1u << 7;
constexpr uint32_t kOpFgTransp = 1u << 8;
constexpr uint32_t kOpBgTransp = 1u << 9;
constexpr uint32_t kOpModeled  = 0x3FFu;

constexpr uint32_t kAlphaFgConst = 0u;
constexpr uint32_t kAlphaBgConst = 1u;
constexpr uint32_t kAlphaFgPixel = 2u;
constexpr uint32_t kCo3FgAlpha   = 1u;
constexpr uint32_t kKeyMask      = 0xFFFu;
constexpr uint32_t kKeyC1Shift   = 16u;
constexpr uint32_t kPixelMax     = (1u << kMsm8255MdpPixelBits) - 1u;
constexpr uint32_t kAlphaMax     = 0xFFu;

uint32_t Scale8(uint32_t a, uint32_t b) {
    return (a * b + kAlphaMax / 2u) / kAlphaMax;
}

uint8_t Weight(uint32_t type, bool inverted, bool modulated, uint8_t own_const, uint8_t fg_const,
               uint8_t bg_const, uint8_t fg_pixel, uint8_t bg_pixel) {
    uint32_t a = type == kAlphaFgConst ? fg_const
               : type == kAlphaBgConst ? bg_const
               : type == kAlphaFgPixel ? fg_pixel
                                       : bg_pixel;
    if (inverted) a = kAlphaMax - a;
    if (modulated) a = Scale8(a, own_const);
    return static_cast<uint8_t>(a);
}

}

bool Msm8255MdpMixer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255MdpMixer::OnReady() { emu_.Get<ParallelJobs>().Require(); }

Msm8255MdpMixer::Key Msm8255MdpMixer::ReadKey(const Msm8255MdpRegisterRead& reg, uint32_t base,
                                             bool enabled) {
    Key key{};
    key.enabled = enabled;
    if (!enabled) return key;
    const uint32_t low0 = reg(base + kTranspLow0), high0 = reg(base + kTranspHigh0);
    key.low  = {static_cast<uint16_t>(low0 & kKeyMask), static_cast<uint16_t>((low0 >> kKeyC1Shift) & kKeyMask),
                static_cast<uint16_t>(reg(base + kTranspLow1) & kKeyMask)};
    key.high = {static_cast<uint16_t>(high0 & kKeyMask), static_cast<uint16_t>((high0 >> kKeyC1Shift) & kKeyMask),
                static_cast<uint16_t>(reg(base + kTranspHigh1) & kKeyMask)};
    return key;
}

bool Msm8255MdpMixer::Matches(const Key& key, const Msm8255MdpPixel& p) {
    const std::array<uint16_t, 3> c = {p.g, p.b, p.r};
    for (uint32_t i = 0; i < 3u; ++i) {
        if (c[i] < key.low[i] || c[i] > key.high[i]) return false;
    }
    return true;
}

const Msm8255MdpMixer::Frame& Msm8255MdpMixer::Compose(const Msm8255MdpRegisterRead& reg) {
    auto& fatal = emu_.Get<Fatal>();
    const uint32_t size = reg(kOvlp0 + kOvlpSize);
    frame_.width  = size & 0xFFFFu;
    frame_.height = size >> 16;
    if (frame_.width == 0u || frame_.height == 0u) {
        fatal.Die("msm8255 mdp mixer: overlay 0 size %08X", size);
    }
    const uint32_t mixer = reg(kRegLayermixerInCfg);
    if ((mixer >> (kPipeCount * kMixerFieldBits)) != 0u) {
        fatal.Die("msm8255 mdp mixer: mixer configuration %08X stages a pipe this chip lacks",
                  mixer);
    }
    std::array<int, kStageIds> stage_pipe;
    stage_pipe.fill(-1);
    for (uint32_t p = 0; p < kPipeCount; ++p) {
        const uint32_t field = (mixer >> (p * kMixerFieldBits)) & 0xFu;
        const uint32_t stage = field & kMixerStageMask;
        if ((field & kMixerOneBit) != 0u || stage == kStageUnused) continue;
        if (stage_pipe[stage] >= 0) {
            fatal.Die("msm8255 mdp mixer: mixer configuration %08X stages two pipes at stage %u",
                      mixer, stage);
        }
        stage_pipe[stage] = static_cast<int>(p);
    }
    if (stage_pipe[kStageBase] < 0) {
        fatal.Die("msm8255 mdp mixer: mixer configuration %08X has no base stage", mixer);
    }
    frame_.pixels.resize(size_t(frame_.width) * frame_.height);
    for (uint32_t stage = kStageBase; stage < kStageIds; ++stage) {
        if (stage_pipe[stage] >= 0) Blend(reg, static_cast<uint32_t>(stage_pipe[stage]), stage);
    }
    return frame_;
}

void Msm8255MdpMixer::Blend(const Msm8255MdpRegisterRead& reg, uint32_t pipe, uint32_t stage) {
    auto& fatal = emu_.Get<Fatal>();
    const Msm8255MdpPipeFetch::Rect dst = emu_.Get<Msm8255MdpPipeFetch>().Fetch(reg, pipe, samples_);
    const bool covers = dst.x == 0u && dst.y == 0u && dst.w == frame_.width && dst.h == frame_.height;
    if (dst.x + dst.w > frame_.width || dst.y + dst.h > frame_.height ||
        (stage == kStageBase && !covers)) {
        fatal.Die("msm8255 mdp mixer: pipe %u at stage %u places %ux%u at %u,%u in a %ux%u overlay",
                  pipe, stage, dst.w, dst.h, dst.x, dst.y, frame_.width, frame_.height);
    }
    auto& color = emu_.Get<Msm8255MdpPipeColor>();
    const Msm8255MdpPipeColor::Path path =
        color.Read(reg, pipe, reg(kPipeBase + pipe * kPipeStride + kPipeOpMode));

    uint32_t op = 0u, co3 = kCo3FgAlpha;
    uint8_t fg_const = static_cast<uint8_t>(kAlphaMax), bg_const = 0u;
    Key fg_key{}, bg_key{};
    if (stage != kStageBase) {
        const uint32_t n = stage - kStageFirst;
        if (n >= kOvlpStageCount) {
            fatal.Die("msm8255 mdp mixer: pipe %u at mixer stage %u has no blend stage", pipe, stage);
        }
        const uint32_t s = kOvlp0 + kOvlpStage0 + n * kOvlpStageStride;
        op       = reg(s + kStageOp);
        co3      = reg(kOvlp0 + kOvlpCo3 + n * kOvlpCo3Stride);
        fg_const = static_cast<uint8_t>(reg(s + kStageFgAlpha));
        bg_const = static_cast<uint8_t>(reg(s + kStageBgAlpha));
        if ((op & ~kOpModeled) != 0u || (co3 & ~kCo3FgAlpha) != 0u ||
            ((op & kOpBgTransp) != 0u && n != 0u)) {
            fatal.Die("msm8255 mdp mixer: stage %u blend op %08X alpha select %08X is not modeled",
                      stage, op, co3);
        }
        fg_key = ReadKey(reg, s + kStageTransp, (op & kOpFgTransp) != 0u);
        bg_key = ReadKey(reg, kOvlp0 + kOvlpBaseTransp, (op & kOpBgTransp) != 0u);
    }
    const uint32_t fg_type = op & 3u, bg_type = (op >> kOpBgShift) & 3u;
    const bool pixel_weights = fg_type >= kAlphaFgPixel || bg_type >= kAlphaFgPixel;
    const bool replaces = stage == kStageBase ||
        (!pixel_weights && !fg_key.enabled && !bg_key.enabled &&
         Weight(fg_type, (op & kOpFgInv) != 0u, (op & kOpFgMod) != 0u, fg_const, fg_const, bg_const,
                0u, 0u) == kAlphaMax &&
         Weight(bg_type, (op & kOpBgInv) != 0u, (op & kOpBgMod) != 0u, bg_const, fg_const, bg_const,
                0u, 0u) == 0u);
    const bool keep_fg_alpha = stage == kStageBase || co3 == kCo3FgAlpha;
    auto& jobs = emu_.Get<ParallelJobs>();
    const uint32_t bands = std::min(dst.h, jobs.Lanes() * kRowBandsPerLane);
    const std::function<void(uint32_t)> band = [&](uint32_t index) {
        const uint32_t y_end = static_cast<uint32_t>(uint64_t(dst.h) * (index + 1u) / bands);
        for (uint32_t y = static_cast<uint32_t>(uint64_t(dst.h) * index / bands); y < y_end; ++y) {
            Msm8255MdpPixel* line = frame_.pixels.data() + size_t(dst.y + y) * frame_.width + dst.x;
            const Msm8255MdpSample* src = samples_.data() + size_t(y) * dst.w;
            for (uint32_t x = 0; x < dst.w; ++x) {
                const Msm8255MdpPixel fg = Msm8255MdpPipeColor::Apply(path, src[x]);
                Msm8255MdpPixel& bg = line[x];
                if (replaces) {
                    bg = Msm8255MdpPixel{fg.r, fg.g, fg.b, keep_fg_alpha ? fg.a : bg.a};
                    continue;
                }
                if (fg_key.enabled && Matches(fg_key, fg)) continue;
                if (bg_key.enabled && Matches(bg_key, bg)) {
                    bg = fg;
                    continue;
                }
                const uint8_t fa = Weight(fg_type, (op & kOpFgInv) != 0u, (op & kOpFgMod) != 0u,
                                          fg_const, fg_const, bg_const, fg.a, bg.a);
                const uint8_t ba = Weight(bg_type, (op & kOpBgInv) != 0u, (op & kOpBgMod) != 0u,
                                          bg_const, fg_const, bg_const, fg.a, bg.a);
                bg = Msm8255MdpPixel{PlaneMix(fg.r, fa, bg.r, ba, kPixelMax), PlaneMix(fg.g, fa, bg.g, ba, kPixelMax),
                                     PlaneMix(fg.b, fa, bg.b, ba, kPixelMax), keep_fg_alpha ? fg.a : bg.a};
            }
        }
    };
    jobs.Run(bands, band);
}

REGISTER_SERVICE(Msm8255MdpMixer);

}
