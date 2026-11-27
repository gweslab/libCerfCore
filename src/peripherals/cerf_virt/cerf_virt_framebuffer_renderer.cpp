#define NOMINMAX

#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_fb_regs.h>

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../host/guest_additions_frame_renderer.h"
#include "cerf_virt_overlay_plane.h"
#include <cerfcore/lcd/lcd_pixel_expand.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace {

class CerfVirtFramebufferRenderer : public GuestAdditionsFrameRenderer {
public:
    using GuestAdditionsFrameRenderer::GuestAdditionsFrameRenderer;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        emu_.Get<CerfVirtFramebuffer>().Geometry(w, h);
    }

    uint32_t DisplayQuarter() override { return emu_.Get<CerfVirtFramebuffer>().Quarter(); }

    bool HasFrame() override { return emu_.Get<CerfVirtFramebuffer>().HasContent(); }

    void RenderInto(uint32_t* dib_bgra32,
                    uint32_t  host_w,
                    uint32_t  host_h) override {
        auto& fb = emu_.Get<CerfVirtFramebuffer>();
        const CerfVirtOverlay overlay = fb.Overlay();
        std::memset(dib_bgra32, 0,
                    static_cast<size_t>(host_w) * host_h * 4u);
        if (overlay.layers != CerfVirt::kFbOverlayLayersOverlay) RenderBase(fb, dib_bgra32, host_w, host_h);
        emu_.Get<CerfVirtOverlayPlane>().Compose(overlay, dib_bgra32, host_w, host_h);
    }

private:
    void RenderBase(CerfVirtFramebuffer& fb, uint32_t* dib_bgra32, uint32_t host_w, uint32_t host_h) {
        const CerfVirtScanout s = fb.Scanout();
        const uint32_t guest_w = s.width;
        const uint32_t bpp     = s.bpp;
        const uint8_t* src     = s.guest_pa != 0u ? LayerBytes(s) : fb.Bytes() + s.offset;

        const uint32_t copy_w = std::min(guest_w, host_w);
        const uint32_t copy_h = std::min(s.height, host_h);
        if (copy_w == 0 || copy_h == 0) return;

        for (uint32_t y = 0; y < copy_h; ++y) {
            const uint8_t* src_row = src + static_cast<size_t>(y) * s.stride;
            uint32_t* dst_row = dib_bgra32 + static_cast<size_t>(y) * host_w;
            if (bpp == 32u) {
                std::memcpy(dst_row, src_row, static_cast<size_t>(copy_w) * 4u);
            } else if (bpp < 8u) {
                const uint32_t* pal = fb.Palette();
                for (uint32_t x = 0; x < copy_w; ++x)
                    dst_row[x] = 0xFF000000u | pal[lcd_pixel::PackedIndexMsbFirst(src_row, x, bpp)];
            } else if (bpp == 8u) {
                const uint32_t* pal = fb.Palette();
                for (uint32_t x = 0; x < copy_w; ++x)
                    dst_row[x] = 0xFF000000u | pal[src_row[x]];
            } else if (bpp == 16u) {
                for (uint32_t x = 0; x < copy_w; ++x)
                    dst_row[x] = lcd_pixel::Expand565(
                        cerf::le::U16(src_row, static_cast<size_t>(x) * 2u));
            } else if (bpp == 24u) {
                for (uint32_t x = 0; x < copy_w; ++x) {
                    dst_row[x] = 0xFF000000u
                               | cerf::le::U24(src_row, static_cast<size_t>(x) * 3u);
                }
            }
        }
    }

    const uint8_t* LayerBytes(const CerfVirtScanout& s) {
        const uint8_t* bytes = emu_.Get<EmulatedMemory>().TryTranslateRange(
            s.guest_pa, uint64_t{s.stride} * s.height);
        if (!bytes)
            emu_.Get<Fatal>().Die("[CerfVirtFramebufferRenderer] layer at 0x%08X, %u x %u B, left "
                                  "backed guest memory", s.guest_pa, s.stride, s.height);
        return bytes;
    }
};

REGISTER_SERVICE_AS(CerfVirtFramebufferRenderer, GuestAdditionsFrameRenderer);

}

}
