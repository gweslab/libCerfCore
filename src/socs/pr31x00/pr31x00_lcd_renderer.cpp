#define NOMINMAX

#include "pr31x00_lcd.h"
#include "pr31x00_lcd_shading.h"
#include "pr31x00_panel.h"

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace {

uint32_t MonoDot(uint32_t on_level) {
    const uint32_t g = 255u - on_level;
    return lcd_pixel::PackXrgb(g, g, g);
}

class Pr31x00LcdRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Pr31500 || soc == SocId::Pr31700;
    }

    void OnReady() override { panel_color_ = emu_.Get<Pr31x00Panel>().Color(); }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        const Pr31x00Lcd::Scanout s = emu_.Get<Pr31x00Lcd>().GetScanout();
        w = Width(s);
        h = s.lines;
    }

    bool HasFrame() override {
        const Pr31x00Lcd::Scanout s = emu_.Get<Pr31x00Lcd>().GetScanout();
        if (!s.enabled)        return false;
        if (latch_.Latched())  return true;
        if (s.fb_pa == 0) return false;
        const size_t fb_bytes = (size_t)s.lines * s.StrideBytes();
        return latch_.ProbeAndLatch(emu_.Get<EmulatedMemory>(),
                                    s.fb_pa, fb_bytes);
    }

    void RenderInto(uint32_t* dib_bgra32, uint32_t host_w, uint32_t host_h) override {
        const Pr31x00Lcd::Scanout s = emu_.Get<Pr31x00Lcd>().GetScanout();
        const auto& shading = emu_.Get<Pr31x00LcdShading>();

        std::memset(dib_bgra32, 0, (size_t)host_w * host_h * 4u);
        if (!s.enabled) return;

        const uint32_t copy_w = std::min(Width(s), host_w);
        const uint32_t copy_h = std::min(s.lines, host_h);

        const uint8_t* src_base = emu_.Get<EmulatedMemory>().TryTranslate(s.fb_pa);
        if (!src_base) return;

        const size_t stride = s.StrideBytes();
        if (s.color_mode) {
            uint32_t xrgb[256];
            shading.FillColorLevels(xrgb, s.inverted);
            for (uint32_t y = 0; y < copy_h; ++y) {
                const uint8_t* src_row = src_base + (size_t)y * stride;
                uint32_t* dst_row = dib_bgra32 + (size_t)y * host_w;
                for (uint32_t x = 0; x < copy_w; ++x) {
                    dst_row[x] = panel_color_
                        ? xrgb[src_row[x]]
                        : MonoDot((xrgb[src_row[x / 3u]] >> (16u - 8u * (x % 3u))) & 0xFFu);
                }
            }
            return;
        }
        uint32_t level[16];
        shading.FillDotLevels(level, s.bits_per_pixel, s.inverted);
        for (uint32_t y = 0; y < copy_h; ++y) {
            const uint8_t* src_row = src_base + (size_t)y * stride;
            uint32_t* dst_row = dib_bgra32 + (size_t)y * host_w;
            auto dot_level = [&](uint32_t d) -> uint32_t {
                return level[lcd_pixel::PackedIndexMsbFirst(src_row, d, s.bits_per_pixel)];
            };
            for (uint32_t x = 0; x < copy_w; ++x) {
                dst_row[x] = panel_color_ ? lcd_pixel::SubPixelPanelPixel(x, s.dots, dot_level)
                                          : MonoDot(x < s.dots ? dot_level(x) : 0u);
            }
        }
    }

private:
    uint32_t Width(const Pr31x00Lcd::Scanout& s) const {
        return panel_color_ ? lcd_pixel::SubPixelPanelWidth(s.dots) : s.dots;
    }

    bool panel_color_ = false;
};

}

REGISTER_SERVICE_AS(Pr31x00LcdRenderer, PanelFrameRenderer);

}
