#include "msm8255_mdp_output.h"

#include "msm8255_id.h"
#include "msm8255_mddi_client.h"
#include "msm8255_mdp_histogram.h"
#include "msm8255_mdp_mixer.h"
#include "msm8255_mdp_pipe_color.h"
#include "msm8255_mdp_regs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>

#include <algorithm>
#include <array>

namespace cerfcore {

namespace {

using namespace msm8255_mdp;

constexpr uint32_t kPrimaryIntfMask  = 3u;
constexpr uint32_t kPrimaryIntfMddi  = 2u;
constexpr uint32_t kOvlpCfgDirectOut = 1u;
constexpr uint32_t kOvlpCfgBlt       = 8u;
constexpr uint32_t kOvlpOpBltFormat  = 3u;
constexpr uint32_t kBltRgb888        = 0u;
constexpr uint32_t kBltRgb565        = 1u;
constexpr uint32_t kBltYcbycr        = 2u;

constexpr CscMatrix kRgbToYcbcr = [] {
    CscMatrix p{};
    p.matrix     = {131, 258, 50, -76, -149, 225, 225, -188, -37};
    p.post_bias  = {16, 128, 128};
    p.pre_clamp  = {0, 255, 0, 255, 0, 255};
    p.post_clamp = {16, 235, 16, 240, 16, 240};
    return p;
}();

constexpr uint32_t kY  = 0u;
constexpr uint32_t kCb = 1u;
constexpr uint32_t kCr = 2u;

std::array<uint32_t, 3> ToYcbcr(const Msm8255MdpPixel& p) {
    return CscConvert(kRgbToYcbcr, {p.r >> 4, p.g >> 4, p.b >> 4});
}

void PackYcbycr(const Msm8255MdpPixel* line, uint32_t width, uint8_t* out) {
    for (uint32_t x = 0; x < width; x += 2u) {
        const std::array<uint32_t, 3> a = ToYcbcr(line[x]);
        const std::array<uint32_t, 3> b = ToYcbcr(line[x + 1u]);
        uint8_t* d = out + size_t(x) * 2u;
        d[0] = static_cast<uint8_t>((a[kCb] + b[kCb] + 1u) / 2u);
        d[1] = static_cast<uint8_t>(a[kY]);
        d[2] = static_cast<uint8_t>((a[kCr] + b[kCr] + 1u) / 2u);
        d[3] = static_cast<uint8_t>(b[kY]);
    }
}
constexpr uint32_t kMddiLdParamDmaP  = 0u;
constexpr uint32_t kMddiLdParamDmaS  = 1u;
constexpr uint32_t kMddiEnabled      = 1u;

constexpr uint32_t kDmaBpcMask       = 0x3Fu;
constexpr uint32_t kDmaPatternShift  = 8u;
constexpr uint32_t kDmaPatternMask   = 0xFFu;
constexpr uint32_t kDmaDither        = 1u << 24;
constexpr uint32_t kDmaInputRgb565   = 1u << 25;
constexpr uint32_t kDmaInputXrgb8888 = 1u << 26;
constexpr uint32_t kDmaConfigModeled = kDmaBpcMask | (kDmaPatternMask << kDmaPatternShift) |
                                       kDmaDither | kDmaInputRgb565 | kDmaInputXrgb8888;

constexpr uint32_t kColorLutEnable = 7u;
constexpr uint32_t kColorLutBank   = 1u << 10;
constexpr uint32_t kColorSelects   = 7u << 10;
constexpr uint32_t kLutEntries     = 256u;

constexpr uint32_t kVideoFormatShift = 13u;
constexpr uint32_t kVideoFormatRgb   = 2u;
constexpr uint32_t kVideoFormatPacked = 1u << 12;

constexpr uint32_t kComponentG = 0u;
constexpr uint32_t kComponentB = 1u;
constexpr uint32_t kComponentR = 2u;


}

bool Msm8255MdpOutput::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

uint32_t Msm8255MdpOutput::PresentOverlay0(const Msm8255MdpRegisterRead& reg) {
    auto& fatal = emu_.Get<Fatal>();
    const uint32_t cfg = reg(kOvlp0 + kOvlpCfg), op_mode = reg(kOvlp0 + kOvlpOpMode);
    const uint32_t blt_format = op_mode & kOvlpOpBltFormat;
    if ((cfg != kOvlpCfgDirectOut && cfg != kOvlpCfgBlt) || (op_mode & ~kOvlpOpBltFormat) != 0u ||
        (cfg == kOvlpCfgBlt && blt_format != kBltRgb565 && blt_format != kBltRgb888 &&
         blt_format != kBltYcbycr)) {
        fatal.Die("msm8255 mdp output: overlay 0 configuration %08X op mode %08X is not modeled",
                  cfg, op_mode);
    }
    const Msm8255MdpMixer::Frame& frame = emu_.Get<Msm8255MdpMixer>().Compose(reg);
    if (cfg == kOvlpCfgBlt) {
        WriteBack(reg, frame, blt_format);
        return 0u;
    }
    const uint32_t dma_size = reg(kDmaP + kDmaSrcSize);
    if ((dma_size & 0xFFFFu) != frame.width || (dma_size >> 16) < frame.height) {
        fatal.Die("msm8255 mdp output: dma_p size %08X for a %ux%u overlay", dma_size, frame.width,
                  frame.height);
    }
    const uint32_t link_bytes = Deliver(reg, kDmaP, kDmaPColor, kMddiLdParamDmaP,
                                        frame.pixels.data(), frame.width, frame.height);
    emu_.Get<Msm8255MdpHistogram>().CountFrame(frame.pixels.data(),
                                              size_t(frame.width) * frame.height);
    return link_bytes;
}

void Msm8255MdpOutput::WriteBack(const Msm8255MdpRegisterRead& reg,
                                 const Msm8255MdpMixer::Frame& frame, uint32_t blt_format) {
    const uint32_t cpp    = blt_format == kBltRgb888 ? 3u : 2u;
    const uint32_t base   = reg(kOvlp0 + kOvlpBase);
    const uint32_t stride = reg(kOvlp0 + kOvlpStride);
    if (blt_format == kBltYcbycr && (frame.width & 1u) != 0u) {
        emu_.Get<Fatal>().Die("msm8255 mdp output: ycbycr write-back of an odd width %u",
                              frame.width);
    }
    auto& mem = emu_.Get<EmulatedMemory>();
    row_.resize(size_t(frame.width) * cpp);
    for (uint32_t y = 0; y < frame.height; ++y) {
        const Msm8255MdpPixel* line = frame.pixels.data() + size_t(y) * frame.width;
        if (blt_format == kBltYcbycr) {
            PackYcbycr(line, frame.width, row_.data());
            mem.CopyIn(base + y * stride, row_.data(), static_cast<uint32_t>(row_.size()));
            continue;
        }
        for (uint32_t x = 0; x < frame.width; ++x) {
            const Msm8255MdpPixel& p = line[x];
            uint8_t* d = row_.data() + size_t(x) * cpp;
            if (blt_format == kBltRgb565) {
                cerf::le::Put16(d, static_cast<uint16_t>(((p.r >> 7) << 11) | ((p.g >> 6) << 5) |
                                                         (p.b >> 7)));
            } else {
                cerf::le::Put24(d, (uint32_t(p.r >> 4) << 16) | (uint32_t(p.g >> 4) << 8) |
                                       uint32_t(p.b >> 4));
            }
        }
        mem.CopyIn(base + y * stride, row_.data(), static_cast<uint32_t>(row_.size()));
    }
}

uint32_t Msm8255MdpOutput::PresentDmaS(const Msm8255MdpRegisterRead& reg) {
    const uint32_t config = reg(kDmaS + kDmaConfig);
    const uint32_t size   = reg(kDmaS + kDmaSrcSize);
    const uint32_t width  = size & 0xFFFFu, height = size >> 16;
    const bool rgb565 = (config & kDmaInputRgb565) != 0u;
    const bool xrgb   = (config & kDmaInputXrgb8888) != 0u;
    if (width == 0u || height == 0u || (rgb565 && xrgb)) {
        emu_.Get<Fatal>().Die("msm8255 mdp output: dma_s configuration %08X size %08X is not "
                              "modeled", config, size);
    }
    const uint32_t cpp    = rgb565 ? 2u : xrgb ? 4u : 3u;
    const uint32_t base   = reg(kDmaS + kDmaSrcBase);
    const uint32_t stride = reg(kDmaS + kDmaSrcStride);
    auto& mem = emu_.Get<EmulatedMemory>();
    dma_s_frame_.resize(size_t(width) * height);
    row_.resize(size_t(width) * cpp);
    for (uint32_t y = 0; y < height; ++y) {
        mem.CopyOut(base + y * stride, row_.data(), static_cast<uint32_t>(row_.size()));
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* s = row_.data() + size_t(x) * cpp;
            const uint32_t xrgb_word = rgb565 ? lcd_pixel::Expand565(cerf::le::U16(s))
                                              : static_cast<uint32_t>(cerf::le::UN(s, cpp));
            dma_s_frame_[size_t(y) * width + x] =
                Msm8255MdpPixel{static_cast<uint16_t>(lcd_pixel::Expand8To12((xrgb_word >> 16) & 0xFFu)),
                                static_cast<uint16_t>(lcd_pixel::Expand8To12((xrgb_word >> 8) & 0xFFu)),
                                static_cast<uint16_t>(lcd_pixel::Expand8To12(xrgb_word & 0xFFu)), 0xFFu};
        }
    }
    return Deliver(reg, kDmaS, kDmaSColor, kMddiLdParamDmaS, dma_s_frame_.data(), width, height);
}

uint32_t Msm8255MdpOutput::Deliver(const Msm8255MdpRegisterRead& reg, uint32_t dma,
                                   uint32_t color, uint32_t ld_param,
                                   const Msm8255MdpPixel* pixels, uint32_t width,
                                   uint32_t height) {
    auto& fatal = emu_.Get<Fatal>();
    const uint32_t color_word = reg(color);
    const uint32_t lut_enable = color_word & kColorLutEnable;
    if ((reg(kRegDispIntfSel) & kPrimaryIntfMask) != kPrimaryIntfMddi ||
        reg(kRegMddiEnable) != kMddiEnabled || reg(kRegMddiLdParam) != ld_param ||
        (color_word & ~(kColorLutEnable | kColorSelects)) != 0u ||
        (lut_enable != 0u && (lut_enable != kColorLutEnable || dma != kDmaP))) {
        fatal.Die("msm8255 mdp output: dma %05X output with interface select %08X, mddi enable "
                  "%u, mddi ld param %u, color correction %08X is not modeled", dma,
                  reg(kRegDispIntfSel), reg(kRegMddiEnable), reg(kRegMddiLdParam), color_word);
    }
    const uint32_t config  = reg(dma + kDmaConfig);
    const uint32_t pattern = (config >> kDmaPatternShift) & kDmaPatternMask;
    const std::array<uint32_t, 3> order = {(pattern >> 4) & 3u, (pattern >> 2) & 3u, pattern & 3u};
    const std::array<uint32_t, 3> bits  = {kBpcBits[config & 3u], kBpcBits[(config >> 2) & 3u],
                                           kBpcBits[(config >> 4) & 3u]};
    const bool distinct = order[0] != order[1] && order[1] != order[2] && order[0] != order[2];
    if ((config & ~kDmaConfigModeled) != 0u || (pattern >> 6) != 0u || !distinct ||
        order[0] > kComponentR || order[1] > kComponentR || order[2] > kComponentR) {
        fatal.Die("msm8255 mdp output: dma %05X configuration %08X is not modeled", dma, config);
    }
    const uint32_t packet = reg(kRegMddiVideoPacket);
    const uint32_t format = packet >> 16;
    if ((format >> kVideoFormatShift) != kVideoFormatRgb ||
        ((format >> 8) & 0xFu) != bits[kComponentR] || ((format >> 4) & 0xFu) != bits[kComponentG] ||
        (format & 0xFu) != bits[kComponentB]) {
        fatal.Die("msm8255 mdp output: mddi video descriptor %04X for dma %05X configuration %08X",
                  format, dma, config);
    }

    std::array<std::array<uint8_t, kLutEntries>, 3> lut{};
    for (uint32_t i = 0; i < kLutEntries; ++i) {
        const uint32_t entry = lut_enable != 0u
            ? reg(kDmaPLut + ((color_word & kColorLutBank) != 0u ? kLutBankStride : 0u) + 4u * i)
            : (i | (i << 8) | (i << 16));
        lut[kComponentG][i] = static_cast<uint8_t>(entry);
        lut[kComponentB][i] = static_cast<uint8_t>(entry >> 8);
        lut[kComponentR][i] = static_cast<uint8_t>(entry >> 16);
    }

    const uint32_t dst = reg(dma + kDmaDstXy);
    const uint32_t dx = dst & 0xFFFFu, dy = dst >> 16;
    Msm8255MddiVideoStream video{};
    video.format_descriptor = static_cast<uint16_t>(format);
    video.pixel_attributes  = static_cast<uint16_t>(packet & 0xFFFFu);
    video.x_left_edge       = static_cast<uint16_t>(dx);
    video.y_top_edge        = static_cast<uint16_t>(dy);
    video.x_right_edge      = static_cast<uint16_t>(dx + width - 1u);
    video.y_bottom_edge     = static_cast<uint16_t>(dy + height - 1u);
    video.x_start           = static_cast<uint16_t>(dx);
    video.pixel_count       = static_cast<uint16_t>(width);
    const uint32_t mid_bits   = bits[order[2]];
    const uint32_t top_bits   = bits[order[1]] + mid_bits;
    const uint32_t pixel_bits = top_bits + bits[order[0]];
    const bool packed         = (format & kVideoFormatPacked) != 0u;
    const uint32_t pixel_bytes = (pixel_bits + 7u) / 8u;
    line_.assign(packed ? (size_t(width) * pixel_bits + 7u) / 8u : size_t(width) * pixel_bytes, 0u);
    auto& client = emu_.Get<Msm8255MddiClient>();
    for (uint32_t y = 0; y < height; ++y) {
        std::fill(line_.begin(), line_.end(), uint8_t{0});
        for (uint32_t x = 0; x < width; ++x) {
            const Msm8255MdpPixel& p = pixels[size_t(y) * width + x];
            const std::array<uint32_t, 3> v = {lut[kComponentG][p.g >> 4], lut[kComponentB][p.b >> 4],
                                               lut[kComponentR][p.r >> 4]};
            const uint32_t word = ((v[order[0]] >> (8u - bits[order[0]])) << top_bits) |
                                  ((v[order[1]] >> (8u - bits[order[1]])) << mid_bits) |
                                  (v[order[2]] >> (8u - bits[order[2]]));
            if (!packed || pixel_bits % 8u == 0u) {
                cerf::le::PutN(line_.data() + size_t(x) * pixel_bytes, word, pixel_bytes);
                continue;
            }
            const size_t bit = size_t(x) * pixel_bits;
            for (uint32_t i = 0; i < pixel_bits; ++i) {
                line_[(bit + i) / 8u] |= static_cast<uint8_t>(((word >> i) & 1u) << ((bit + i) % 8u));
            }
        }
        video.y_start = static_cast<uint16_t>(dy + y);
        client.WriteVideoStream(video, line_.data(), static_cast<uint32_t>(line_.size()));
    }
    return height * (kMsm8255MddiVideoStreamHeaderBytes + static_cast<uint32_t>(line_.size()) +
                     kMsm8255MddiPixelDataCrcBytes);
}

REGISTER_SERVICE(Msm8255MdpOutput);

}
