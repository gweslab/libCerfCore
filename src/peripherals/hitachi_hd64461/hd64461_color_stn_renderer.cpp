#define NOMINMAX

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "hd64461_lcdc.h"
#include "hd64461_panel.h"

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace {

using Format = Hd64461Lcdc::Format;

class Hd64461ColorStnRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600 &&
               emu_.Get<Hd64461Panel>().PanelKind() == Hd64461Panel::Kind::kColorStn8Bit;
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        const Hd64461Lcdc::Scanout s = emu_.Get<Hd64461Lcdc>().GetScanout();
        w = s.presented_width;
        h = s.lines;
    }

    bool HasFrame() override {
        const Hd64461Lcdc::Scanout s = emu_.Get<Hd64461Lcdc>().GetScanout();
        if (!Shows(s)) return false;
        if (latch_.Latched()) return true;
        return latch_.ProbeAndLatch(s.fb, s.frame_bytes);
    }

    void RenderInto(uint32_t* dib_bgra32, uint32_t host_w, uint32_t host_h) override {
        const Hd64461Lcdc::Scanout s = emu_.Get<Hd64461Lcdc>().GetScanout();
        std::memset(dib_bgra32, 0, (size_t)host_w * host_h * 4u);
        if (!Shows(s)) return;
        const uint32_t copy_w = std::min(s.presented_width, host_w);
        const uint32_t copy_h = std::min(s.lines, host_h);
        switch (s.format) {
        case Format::kIndexed8: RenderIndexed8(s, dib_bgra32, host_w, copy_w, copy_h); return;
        case Format::kRgb565:   RenderRgb565(s, dib_bgra32, host_w, copy_w, copy_h); return;
        case Format::kGray64:   RenderGrayDots(s, 6u, dib_bgra32, host_w, copy_w, copy_h); return;
        case Format::kGray16:   RenderGrayDots(s, 4u, dib_bgra32, host_w, copy_w, copy_h); return;
        case Format::kGray4:    RenderGrayDots(s, 2u, dib_bgra32, host_w, copy_w, copy_h); return;
        case Format::kGray2:    RenderGrayDots(s, 1u, dib_bgra32, host_w, copy_w, copy_h); return;
        case Format::kUndefined: return;
        }
    }

private:
    static bool Shows(const Hd64461Lcdc::Scanout& s) {
        if (!s.presenting || s.format == Format::kUndefined) return false;
        if (s.format == Format::kIndexed8) return s.palette_complete;
        return s.line_step == 1u;
    }

    static const uint8_t* Row(const Hd64461Lcdc::Scanout& s, uint32_t y) {
        return s.fb + (size_t)y * s.line_step * s.stride;
    }

    static void RenderIndexed8(const Hd64461Lcdc::Scanout& s, uint32_t* dib, uint32_t host_w, uint32_t copy_w,
                               uint32_t copy_h) {
        uint32_t lut[256];
        for (uint32_t i = 0; i < 256u; ++i) {
            lut[i] = lcd_pixel::PackXrgb(lcd_pixel::Expand6(s.palette[i * 3u]),
                                         lcd_pixel::Expand6(s.palette[i * 3u + 1u]),
                                         lcd_pixel::Expand6(s.palette[i * 3u + 2u]));
        }
        for (uint32_t y = 0; y < copy_h; ++y) {
            const uint8_t* row = Row(s, y);
            uint32_t*      dst = dib + (size_t)y * host_w;
            for (uint32_t x = 0; x < copy_w; ++x) dst[x] = lut[row[x]];
        }
    }

    static void RenderRgb565(const Hd64461Lcdc::Scanout& s, uint32_t* dib, uint32_t host_w, uint32_t copy_w,
                             uint32_t copy_h) {
        for (uint32_t y = 0; y < copy_h; ++y) {
            const uint8_t* row = Row(s, y);
            uint32_t*      dst = dib + (size_t)y * host_w;
            for (uint32_t x = 0; x < copy_w; ++x) dst[x] = lcd_pixel::Expand565(cerf::le::U16(row, x * 2u));
        }
    }

    static void RenderGrayDots(const Hd64461Lcdc::Scanout& s, uint32_t bits, uint32_t* dib, uint32_t host_w,
                               uint32_t copy_w, uint32_t copy_h) {
        for (uint32_t y = 0; y < copy_h; ++y) {
            const uint8_t* row = Row(s, y);
            uint32_t*      dst = dib + (size_t)y * host_w;
            const auto     dot = [&](uint32_t d) {
                const uint32_t level = bits == 6u ? lcd_pixel::Expand6(row[d] & 0x3Fu)
                                                  : lcd_pixel::PackedIndexMsbFirst(row, d, bits) * (255u / ((1u << bits) - 1u));
                return s.dinv ? 255u - level : level;
            };
            for (uint32_t x = 0; x < copy_w; ++x) dst[x] = lcd_pixel::SubPixelPanelPixel(x, s.dots, dot);
        }
    }
};

}

REGISTER_SERVICE_AS(Hd64461ColorStnRenderer, PanelFrameRenderer);

}
