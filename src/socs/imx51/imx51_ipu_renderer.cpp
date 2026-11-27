#define NOMINMAX

#include "imx51_ipu_cpmem.h"
#include "imx51_ipu_srm.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/ford_sync_gen2/ford_sync_gen2_family_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../host/panel_frame_renderer.h"
#include <cerfcore/lcd/lcd_pixel_expand.h>

#include <cstring>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kBppRgb565 = 3u;   /* CPMEM BPP code for 16bpp (ipu-cpmem.c) */
constexpr uint32_t kBpp32     = 0u;   /* CPMEM BPP code for 32bpp (ipu-cpmem.c) */

/* DP_COM_CONF_SYNC (RM Table 42-116). */
constexpr uint32_t kDpFgEn         = 0x1u; /* FG_EN[0]: partial (FG) plane enabled */
constexpr uint32_t kDpComComposite = 0x3u; /* FG_EN|GWSEL: partial plane, GWAM=0 local alpha */
constexpr uint32_t kDpComGlobal    = 0x7u; /* FG_EN|GWSEL|GWAM: partial plane, global alpha */
constexpr uint32_t kFgPfsRgba8888  = 7u;

/* DP graphic-window local per-pixel alpha (RM Table 42-116, GWAM=0):
   out = (A*FG + (255-A)*BG)/255 per channel, round-to-nearest. FG/BG/out are
   0xAARRGGBB words (host DIB BGRA byte order); alpha = FG>>24 (live: FG px b3). */
inline uint32_t BlendLocalAlpha(uint32_t fg, uint32_t bg) {
    const uint32_t a  = fg >> 24;
    const uint32_t ia = 255u - a;
    auto ch = [&](uint32_t sh) -> uint32_t {
        return (a * ((fg >> sh) & 0xFFu) + ia * ((bg >> sh) & 0xFFu) + 127u) / 255u;
    };
    return lcd_pixel::PackXrgb(ch(16), ch(8), ch(0));
}

class Imx51IpuRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        const auto d = ActiveDisplay();
        w = d.valid ? d.fw : 0u;
        h = d.valid ? d.fh : 0u;
    }

    bool HasFrame() override { return ActiveDisplay().valid; }

    void RenderInto(uint32_t* dib_bgra32,
                    uint32_t  host_w,
                    uint32_t  host_h) override {
        std::memset(dib_bgra32, 0, static_cast<size_t>(host_w) * host_h * 4u);
        const auto bg = ActiveDisplay();
        if (!bg.valid) return;
        if (bg.bpp != kBppRgb565) {
            emu_.Get<Fatal>().Die("Imx51IpuRenderer: unimplemented BG scanout format eba=0x%08X "
                                  "%ux%u sl=%u bpp_code=%u pfs=%u",
                                  bg.eba, bg.fw, bg.fh, bg.sl, bg.bpp, bg.pfs);
        }

        Imx51IpuChannelDesc fg;
        if (!FgActive(fg)) { BlitBg(dib_bgra32, host_w, host_h, bg); return; }

        auto& srm = emu_.Get<Imx51IpuSrm>();
        const uint32_t comconf = srm.DpComConfSync();
        const uint32_t fgpos   = srm.DpFgPosSync();
        const bool common_ok = (fgpos == 0u && fg.bpp == kBpp32 &&
                                fg.pfs == kFgPfsRgba8888 && fg.alu == 0u &&
                                fg.fw == bg.fw && fg.fh == bg.fh);
        if (common_ok && comconf == kDpComComposite) {
            CompositeFgOverBg(dib_bgra32, host_w, host_h, bg, fg);
            return;
        }
        /* GWAV[31:24]==0 == FG totally opaque (RM Table 42-117); non-zero GWAV is a
           partial global-alpha blend whose combine arithmetic the RM does not state
           as an equation - left FATAL until grounded, never guessed. */
        if (common_ok && comconf == kDpComGlobal &&
            (srm.DpGraphWindCtrlSync() >> 24) == 0u) {
            CompositeFgOpaque(dib_bgra32, host_w, host_h, fg);
            return;
        }
        emu_.Get<Fatal>().Die("Imx51IpuRenderer: unmodeled DP composition COM_CONF=0x%08X "
                              "GWCTRL=0x%08X FG_POS=0x%08X | FG eba1=0x%08X %ux%u bpp=%u "
                              "pfs=%u alu=%u | BG %ux%u",
                              comconf, srm.DpGraphWindCtrlSync(), fgpos, fg.eba1, fg.fw, fg.fh,
                              fg.bpp, fg.pfs, fg.alu, bg.fw, bg.fh);
    }

private:
    struct Scanout {
        const char*          name   = nullptr;
        uint32_t             base   = 0;
        uint32_t             stride = 0;
        uint32_t             bytes  = 0;
        const uint8_t*       span   = nullptr;
        std::vector<uint8_t> row;
    };

    Scanout OpenScanout(const char* name, uint32_t base, uint32_t stride, uint32_t bytes, uint32_t rows) {
        Scanout plane{name, base, stride, bytes, nullptr, {}};
        plane.span = emu_.Get<EmulatedMemory>().TryTranslateRange(base, uint64_t(rows - 1u) * stride + bytes);
        if (!plane.span) plane.row.resize(bytes);
        return plane;
    }

    const uint8_t* ScanoutRow(Scanout& plane, uint32_t y) {
        if (plane.span) return plane.span + static_cast<size_t>(y) * plane.stride;
        const uint64_t address = uint64_t(plane.base) + uint64_t(y) * plane.stride;
        const uint32_t copied = address + plane.bytes - 1u > UINT32_MAX ? 0u :
            emu_.Get<EmulatedMemory>().TryCopyOut(static_cast<uint32_t>(address), plane.row.data(), plane.bytes);
        if (copied != plane.bytes)
            emu_.Get<Fatal>().Die("Imx51IpuRenderer: %s scanout byte 0x%llX is not guest memory", plane.name,
                                  static_cast<unsigned long long>(address + copied));
        return plane.row.data();
    }

    Imx51IpuChannelDesc ActiveDisplay() {
        auto* cp = emu_.TryGet<Imx51IpuCpmem>();
        if (!cp) return {};
        for (uint32_t ch : {23u, 28u, 27u, 24u}) {
            const auto d = cp->DecodeChannel(ch);
            if (d.valid) return d;
        }
        return {};
    }

    bool FgActive(Imx51IpuChannelDesc& fg) {
        auto* cp = emu_.TryGet<Imx51IpuCpmem>();
        if (!cp) return false;
        if (!(emu_.Get<Imx51IpuSrm>().DpComConfSync() & kDpFgEn)) return false;
        fg = cp->DecodeChannel(27u);
        return fg.eba1 != 0u && fg.fw > 1u && fg.fh > 1u;
    }

    void BlitBg(uint32_t* dib, uint32_t host_w, uint32_t host_h,
                const Imx51IpuChannelDesc& bg) {
        const uint32_t cw = (bg.fw < host_w) ? bg.fw : host_w;
        const uint32_t ch = (bg.fh < host_h) ? bg.fh : host_h;
        if (cw == 0u || ch == 0u) return;
        Scanout plane = OpenScanout("background", bg.eba, bg.sl, cw * 2u, ch);
        for (uint32_t y = 0; y < ch; ++y) {
            const uint8_t* srow = ScanoutRow(plane, y);
            uint32_t* drow = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x)
                drow[x] = lcd_pixel::Expand565(cerf::le::U16(srow, static_cast<size_t>(x) * 2u));
        }
    }

    void CompositeFgOverBg(uint32_t* dib, uint32_t host_w, uint32_t host_h,
                           const Imx51IpuChannelDesc& bg,
                           const Imx51IpuChannelDesc& fg) {
        const uint32_t fg_stride = fg.fw * 4u;
        const uint32_t cw = (fg.fw < host_w) ? fg.fw : host_w;
        const uint32_t ch = (fg.fh < host_h) ? fg.fh : host_h;
        if (cw == 0u || ch == 0u) return;
        Scanout fg_plane = OpenScanout("foreground", fg.eba1, fg_stride, cw * 4u, ch);
        Scanout bg_plane = OpenScanout("background", bg.eba, bg.sl, cw * 2u, ch);
        for (uint32_t y = 0; y < ch; ++y) {
            const uint8_t* frow = ScanoutRow(fg_plane, y);
            const uint8_t* brow = ScanoutRow(bg_plane, y);
            uint32_t* drow = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x) {
                const uint32_t bgpx = lcd_pixel::Expand565(cerf::le::U16(brow, static_cast<size_t>(x) * 2u));
                drow[x] = BlendLocalAlpha(cerf::le::U32(frow, static_cast<size_t>(x) * 4u), bgpx);
            }
        }
    }

    /* Global-alpha opaque overlay (RM Table 42-116 GWAM=1, Table 42-117 GWAV=0):
       the FG graphic window fully replaces the BG; per-pixel FG alpha is not used
       in global mode. FG is packed RGBA8888 at frame origin (FG_POS=0). */
    void CompositeFgOpaque(uint32_t* dib, uint32_t host_w, uint32_t host_h,
                           const Imx51IpuChannelDesc& fg) {
        const uint32_t fg_stride = fg.fw * 4u;
        const uint32_t cw = (fg.fw < host_w) ? fg.fw : host_w;
        const uint32_t ch = (fg.fh < host_h) ? fg.fh : host_h;
        if (cw == 0u || ch == 0u) return;
        Scanout plane = OpenScanout("foreground", fg.eba1, fg_stride, cw * 4u, ch);
        for (uint32_t y = 0; y < ch; ++y) {
            const uint8_t* frow = ScanoutRow(plane, y);
            uint32_t* drow = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x)
                drow[x] = 0xFF000000u | cerf::le::U24(frow, static_cast<size_t>(x) * 4u);
        }
    }
};

}

REGISTER_SERVICE_AS(Imx51IpuRenderer, PanelFrameRenderer);

}
