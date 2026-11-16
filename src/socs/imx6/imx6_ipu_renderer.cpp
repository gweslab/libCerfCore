#define NOMINMAX

#include "imx6_ipu.h"
#include "imx6_ipu_cpmem.h"
#include "imx6_ipu_dp_srm_state.h"
#include "imx6_ipu_display_signal.h"
#include "imx6_ipu_lut.h"
#include "imx6_ipu_register_map.h"
#include "imx6_iomuxc.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../host/panel_frame_renderer.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "imx6_id.h"

namespace cerfcore {

using namespace imx6_ipu;

namespace {

constexpr uint32_t kBitsRgb565 = 16u;

inline uint32_t Expand565(uint16_t px) {
    const uint8_t r5 = (px >> 11) & 0x1Fu;
    const uint8_t g6 = (px >> 5) & 0x3Fu;
    const uint8_t b5 = px & 0x1Fu;
    const uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    const uint8_t g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    const uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

bool UsesRgb565Layout(const Imx6IpuChannelDesc& d) {
    return d.bits_per_pixel == kBitsRgb565 && d.pfs == 7u && d.component_width[0] == 5u &&
           d.component_width[1] == 6u && d.component_width[2] == 5u && d.component_offset[0] == 0u &&
           d.component_offset[1] == 5u && d.component_offset[2] == 11u;
}

}

namespace {

class Imx6IpuRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }

    bool HasFrame() override {
        const auto d = ActiveDisplay();
        if (!d.valid || !LvdsLinkFromDi0()) return false;
        return true;
    }

    void RenderInto(uint32_t* dib_bgra32, uint32_t host_w, uint32_t host_h) override {
        std::memset(dib_bgra32, 0, static_cast<size_t>(host_w) * host_h * 4u);
        const auto d = ActiveDisplay();
        if (!d.valid || !LvdsLinkFromDi0()) return;
        DrawChannel(dib_bgra32, host_w, host_h, d, 0u, 0u);
        CompositeAuxiliary(dib_bgra32, host_w, host_h, d);
        if (!LvdsLink24Bit()) ReduceTo18Bit(dib_bgra32, static_cast<size_t>(host_w) * host_h);
    }

    static uint64_t RowBytes(const Imx6IpuChannelDesc& d, uint32_t pixels) {
        return (static_cast<uint64_t>(pixels) * d.bits_per_pixel + 7u) / 8u;
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        const auto d = ActiveDisplay();
        w = d.valid ? d.fw : 0u;
        h = d.valid ? d.fh : 0u;
    }

private:
    uint32_t Gpr2() const { return emu_.Get<Imx6Iomuxc>().PadRegister(Imx6Iomuxc::kGpr2); }

    bool LvdsLinkFromDi0() const {
        const uint32_t gpr2 = Gpr2();
        return (gpr2 & 3u) == 1u || ((gpr2 >> 2) & 3u) == 1u;
    }

    bool LvdsLink24Bit() const {
        const uint32_t gpr2 = Gpr2();
        return (gpr2 & 3u) == 1u ? (gpr2 & (1u << 5)) != 0u : (gpr2 & (1u << 7)) != 0u;
    }

    static void ReduceTo18Bit(uint32_t* pixels, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            const uint32_t six = pixels[i] & 0x00FCFCFCu;
            pixels[i] = (pixels[i] & 0xFF000000u) | six | ((six >> 6) & 0x00030303u);
        }
    }

    void DrawChannel(uint32_t* dib_bgra32, uint32_t host_w, uint32_t host_h, const Imx6IpuChannelDesc& d,
                     uint32_t dst_x, uint32_t dst_y) {
        if (d.pfs != 5u && d.pfs != 7u)
            emu_.Get<Fatal>().Die("i.MX6 IPU: display channel PFS %u is not supported", d.pfs);
        if (d.pfs == 5u && d.bits_per_pixel != 4u && d.bits_per_pixel != 8u)
            emu_.Get<Fatal>().Die("i.MX6 IPU: PFS 5 LUT requires 4-bit or 8-bit coded pixels, got %u bits",
                                  d.bits_per_pixel);
        if (dst_x >= host_w || dst_y >= host_h) return;

        const uint32_t cw = std::min<uint32_t>(d.fw, host_w - dst_x);
        const uint32_t ch = std::min<uint32_t>(d.fh, host_h - dst_y);
        const uint64_t span = ch != 0u ? static_cast<uint64_t>(ch - 1u) * d.sl + RowBytes(d, cw) : 0u;
        const uint8_t* src = emu_.Get<EmulatedMemory>().TryTranslateRange(d.eba, span);
        if (!src)
            emu_.Get<Fatal>().Die("i.MX6 IPU: display channel EBA 0x%08X is not backed memory "
                                  "for %llu bytes",
                                  d.eba, static_cast<unsigned long long>(span));

        if (UsesRgb565Layout(d)) {
            for (uint32_t y = 0; y < ch; ++y) {
                const uint8_t* srow = src + static_cast<size_t>(y) * d.sl;
                uint32_t* drow = dib_bgra32 + static_cast<size_t>(dst_y + y) * host_w + dst_x;
                for (uint32_t x = 0; x < cw; ++x)
                    drow[x] = Expand565(cerf::le::U16(srow, static_cast<size_t>(x) * 2u));
            }
        } else {
            CodedLayout layout{};
            layout.coded_bits = d.bits_per_pixel;
            layout.coded_mask = (1ull << layout.coded_bits) - 1ull;
            layout.pfs = d.pfs;
            layout.dec_sel = d.dec_sel;
            if (d.pfs != 5u) {
                for (uint32_t component = 0; component < 3u; ++component) {
                    const uint32_t width = d.component_width[component];
                    const uint32_t offset = d.component_offset[component];
                    if (offset + width > layout.coded_bits)
                        emu_.Get<Fatal>().Die("i.MX6 IPU: CPMEM component %u width %u offset %u "
                                              "exceeds %u-bit pixel",
                                              component, width, offset, layout.coded_bits);
                    layout.shift[component] = layout.coded_bits - offset - width;
                    layout.mask[component] = (1u << width) - 1u;
                }
            }
            if (d.pfs != 5u && !d.separate_alpha) {
                const uint32_t width = d.component_width[3];
                const uint32_t offset = d.component_offset[3];
                if (offset + width <= layout.coded_bits) {
                    const uint32_t shift = layout.coded_bits - offset - width;
                    const uint64_t alpha_bits = ((1ull << width) - 1ull) << shift;
                    bool overlaps = false;
                    for (uint32_t component = 0; component < 3u; ++component) {
                        const uint64_t component_bits = static_cast<uint64_t>(layout.mask[component]) << layout.shift[component];
                        overlaps = overlaps || (alpha_bits & component_bits) != 0u;
                    }
                    layout.alpha_from_pixel = !overlaps;
                    layout.shift[3] = shift;
                    layout.mask[3] = (1u << width) - 1u;
                }
            }
            const uint32_t scaled_components = layout.alpha_from_pixel ? 4u : (d.pfs != 5u ? 3u : 0u);
            bool identity = d.pfs != 5u;
            for (uint32_t component = 0; component < scaled_components; ++component) {
                const uint32_t mask = layout.mask[component];
                for (uint32_t value = 0; value <= mask; ++value)
                    layout.scale[component][value] = static_cast<uint8_t>((value * 255u + mask / 2u) / mask);
                identity = identity && mask == 0xFFu;
            }
            if (identity)
                ConvertRows<true>(layout, src, d.sl, dib_bgra32, host_w, dst_x, dst_y, cw, ch);
            else
                ConvertRows<false>(layout, src, d.sl, dib_bgra32, host_w, dst_x, dst_y, cw, ch);
        }
    }

    struct CodedLayout {
        uint32_t coded_bits = 0;
        uint64_t coded_mask = 0;
        uint32_t pfs = 0;
        uint32_t dec_sel = 0;
        uint32_t shift[4]{};
        uint32_t mask[4]{};
        bool alpha_from_pixel = false;
        uint8_t scale[4][256]{};
    };

    template <bool kIdentityScale>
    void ConvertRows(const CodedLayout& layout, const uint8_t* src, uint32_t stride, uint32_t* dib_bgra32,
                            uint32_t host_w, uint32_t dst_x, uint32_t dst_y, uint32_t cw, uint32_t ch) {
        const uint32_t coded_bits = layout.coded_bits;
        const uint64_t coded_mask = layout.coded_mask;
        const bool lut_coded = layout.pfs == 5u;
        const Imx6IpuLut* lut = lut_coded ? &emu_.Get<Imx6IpuLut>() : nullptr;
        const bool alpha_from_pixel = layout.alpha_from_pixel;
        const uint32_t shift0 = layout.shift[0], shift1 = layout.shift[1], shift2 = layout.shift[2], shift3 = layout.shift[3];
        const uint32_t mask0 = layout.mask[0], mask1 = layout.mask[1], mask2 = layout.mask[2], mask3 = layout.mask[3];
        const uint8_t* scale0 = layout.scale[0];
        const uint8_t* scale1 = layout.scale[1];
        const uint8_t* scale2 = layout.scale[2];
        const uint8_t* scale3 = layout.scale[3];
        for (uint32_t y = 0; y < ch; ++y) {
            const uint8_t* srow = src + static_cast<size_t>(y) * stride;
            uint32_t* drow = dib_bgra32 + static_cast<size_t>(dst_y + y) * host_w + dst_x;
            for (uint32_t x = 0; x < cw; ++x) {
                uint64_t pixel;
                if (coded_bits == 32u) {
                    pixel = cerf::le::U32(srow, static_cast<size_t>(x) * 4u);
                } else {
                    const uint64_t pixel_bit = static_cast<uint64_t>(x) * coded_bits;
                    const size_t byte_off = static_cast<size_t>(pixel_bit >> 3);
                    const uint32_t bit_off = static_cast<uint32_t>(pixel_bit & 7u);
                    const size_t read_bytes = (bit_off + coded_bits + 7u) / 8u;
                    pixel = (cerf::le::UN(srow + byte_off, read_bytes) >> bit_off) & coded_mask;
                }
                uint32_t r, g, b;
                uint32_t alpha = 0xFFu;
                if (lut_coded) {
                    pixel = lut->Decode(static_cast<uint32_t>(pixel), layout.dec_sel, coded_bits == 4u);
                    r = static_cast<uint8_t>(pixel >> 24);
                    g = static_cast<uint8_t>(pixel >> 16);
                    b = static_cast<uint8_t>(pixel >> 8);
                    alpha = static_cast<uint8_t>(pixel);
                } else {
                    r = static_cast<uint32_t>(pixel >> shift0) & mask0;
                    g = static_cast<uint32_t>(pixel >> shift1) & mask1;
                    b = static_cast<uint32_t>(pixel >> shift2) & mask2;
                    if (!kIdentityScale) {
                        r = scale0[r];
                        g = scale1[g];
                        b = scale2[b];
                    }
                }
                if (alpha_from_pixel) {
                    alpha = static_cast<uint32_t>(pixel >> shift3) & mask3;
                    if (!kIdentityScale) alpha = scale3[alpha];
                }
                drow[x] = (alpha << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }

    void CompositeAuxiliary(uint32_t* dib_bgra32, uint32_t host_w, uint32_t host_h,
                            const Imx6IpuChannelDesc& main) {
        auto& cp = emu_.Get<Imx6IpuCpmem>();
        auto& ipu = emu_.Get<Imx6Ipu>();
        if (!ipu.IdmacChannelEnabled(27u)) return;

        const uint32_t conf = emu_.Get<Imx6IpuDpSrmState>().Register(kOffDpComConfSync);
        if ((conf & 1u) == 0u) return;
        if (emu_.Get<Imx6IpuDisplaySignal>().ActiveDisplayChannel() != 23u)
            emu_.Get<Fatal>().Die("i.MX6 DP: auxiliary channel 27 is enabled without primary channel 23");
        if (ipu.IdmacChannelEnabled(29u))
            emu_.Get<Fatal>().Die("i.MX6 DP: secondary auxiliary channel 29 is not modelled");
        if ((conf & 0x00003F70u) != 0u)
            emu_.Get<Fatal>().Die("i.MX6 DP: DP_COM_CONF CSC/gamma/cursor fields are not modelled (0x%08X)", conf);
        if ((conf & ~0x00003F7Fu) != 0u)
            emu_.Get<Fatal>().Die("i.MX6 DP: DP_COM_CONF reserved fields are set (0x%08X)", conf);

        const Imx6IpuChannelDesc auxiliary = cp.DecodeChannel(27u);
        if (!auxiliary.valid)
            emu_.Get<Fatal>().Die("i.MX6 DP: enabled auxiliary channel 27 has invalid CPMEM parameters");
        const uint32_t control = emu_.Get<Imx6IpuDpSrmState>().Register(kOffDpGraphWindCtrlSync);
        const uint32_t position = emu_.Get<Imx6IpuDpSrmState>().Register(kOffDpFgPosSync);
        if ((position & ~0x07FF07FFu) != 0u)
            emu_.Get<Fatal>().Die("i.MX6 DP: DP_FG_POS reserved fields are set (0x%08X)", position);
        const uint32_t dst_x = (position >> 16) & 0x7FFu;
        const uint32_t dst_y = position & 0x7FFu;
        std::vector<uint32_t> auxiliary_pixels(static_cast<size_t>(host_w) * host_h);
        DrawChannel(auxiliary_pixels.data(), host_w, host_h, auxiliary, dst_x, dst_y);

        const bool graphic_is_partial = (conf & (1u << 1)) != 0u;
        const bool global_alpha = (conf & (1u << 2)) != 0u;
        const bool color_key = (conf & (1u << 3)) != 0u;
        uint32_t alpha = (control >> 24) & 0xFFu;
        if (alpha >= 128u) ++alpha;
        const uint8_t key_r = static_cast<uint8_t>(control >> 16);
        const uint8_t key_g = static_cast<uint8_t>(control >> 8);
        const uint8_t key_b = static_cast<uint8_t>(control);

        const uint8_t* local_alpha = nullptr;
        uint32_t alpha_stride = 0u;
        if (!global_alpha) {
            const Imx6IpuChannelDesc& graphic = graphic_is_partial ? auxiliary : main;
            if (graphic.separate_alpha) {
                if (!graphic_is_partial || !ipu.IdmacChannelEnabled(31u))
                    emu_.Get<Fatal>().Die("i.MX6 DP: separate local alpha is not modelled for the selected plane");
                const Imx6IpuChannelDesc alpha_desc = cp.DecodeChannel(31u);
                if (!alpha_desc.valid || alpha_desc.bits_per_pixel != 8u || alpha_desc.pfs != 6u ||
                    alpha_desc.fw < auxiliary.fw || alpha_desc.fh < auxiliary.fh)
                    emu_.Get<Fatal>().Die("i.MX6 DP: channel 31 is not an 8-bit local alpha plane");
                alpha_stride = alpha_desc.sl;
                const uint64_t alpha_span = auxiliary.fh != 0u
                                                ? static_cast<uint64_t>(auxiliary.fh - 1u) * alpha_stride + auxiliary.fw
                                                : 0u;
                local_alpha = emu_.Get<EmulatedMemory>().TryTranslateRange(alpha_desc.eba, alpha_span);
                if (!local_alpha)
                    emu_.Get<Fatal>().Die("i.MX6 DP: channel 31 local alpha EBA 0x%08X is not backed memory",
                                          alpha_desc.eba);
            }
        }

        const uint32_t cw = dst_x < host_w ? std::min<uint32_t>(auxiliary.fw, host_w - dst_x) : 0u;
        const uint32_t ch = dst_y < host_h ? std::min<uint32_t>(auxiliary.fh, host_h - dst_y) : 0u;
        for (uint32_t y = 0; y < ch; ++y) {
            for (uint32_t x = 0; x < cw; ++x) {
                const size_t index = static_cast<size_t>(dst_y + y) * host_w + dst_x + x;
                const uint32_t partial = auxiliary_pixels[index];
                const uint32_t full = dib_bgra32[index];
                const uint32_t graphic = graphic_is_partial ? partial : full;
                const uint32_t foreground = graphic_is_partial ? partial : full;
                const uint32_t background = graphic_is_partial ? full : partial;
                if (color_key && ((graphic >> 16) & 0xFFu) == key_r && ((graphic >> 8) & 0xFFu) == key_g &&
                    (graphic & 0xFFu) == key_b) {
                    dib_bgra32[index] = background;
                    continue;
                }

                uint32_t pixel_alpha = alpha;
                if (!global_alpha) {
                    pixel_alpha = local_alpha ? local_alpha[static_cast<size_t>(y) * alpha_stride + x]
                                              : ((graphic >> 24) & 0xFFu);
                    if (pixel_alpha >= 128u) ++pixel_alpha;
                }
                uint32_t result = 0xFF000000u;
                for (uint32_t shift : {0u, 8u, 16u}) {
                    const uint32_t fg = (foreground >> shift) & 0xFFu;
                    const uint32_t bg = (background >> shift) & 0xFFu;
                    const uint32_t value = (fg * (256u - pixel_alpha) + bg * pixel_alpha + 128u) >> 8;
                    result |= value << shift;
                }
                dib_bgra32[index] = result;
            }
        }
    }

    Imx6IpuChannelDesc ActiveDisplay() {
        auto& cp = emu_.Get<Imx6IpuCpmem>();
        const uint32_t ch = emu_.Get<Imx6IpuDisplaySignal>().ActiveDisplayChannel();
        if (ch < 64u) {
            return cp.DecodeChannel(ch);
        }
        return {};
    }

};

}

REGISTER_SERVICE_AS(Imx6IpuRenderer, PanelFrameRenderer);

}
