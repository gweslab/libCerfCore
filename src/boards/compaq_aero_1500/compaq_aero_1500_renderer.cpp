#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>
#include "../../peripherals/nec_vrc4171a/nec_vrc4171a_frame_memory.h"
#include "../../peripherals/nec_vrc4171a/nec_vrc4171a_lcd.h"
#include "../../peripherals/nec_vrc4171a/nec_vrc4171a_lcd_scanout.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "compaq_aero_1500_family_id.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

namespace cerfcore {

namespace {

constexpr int      kPanelPowerGpio = 31;
constexpr uint32_t kLevelMax       = 0x3Fu;

class CompaqAero1500Renderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    void OnReady() override {
        lcd_   = &emu_.Get<NecVrc4171aLcd>();
        frame_ = &emu_.Get<NecVrc4171aFrameMemory>();
        giu_   = &emu_.Get<Vr41xxGiu>();
        scanout_.emplace(frame_->Data());
        glass_ = *emu_.Get<BoardDatabase>().FindBoard(emu_.Get<BoardContext>().GetBoardId())->lcd_panel_size;
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        lcd_->Scan(scan_);
        w = ScanLines();
        h = ScanColumns();
    }

    bool HasFrame() override {
        lcd_->Scan(scan_);
        const bool visible =
            scan_.display_on && scan_.vdd_on && giu_->OutputDrive(kPanelPowerGpio) == Vr41xxPinDrive::kHigh;
        lcd_->PublishPanelEnable(visible);
        if (!visible) return false;
        if (latch_.Latched()) return true;
        const uint32_t start = scan_.start & (scan_.memory_bytes - 1u);
        const size_t   bytes = std::min<size_t>(static_cast<size_t>(scan_.stride) * scan_.height,
                                                scan_.memory_bytes - start);
        return latch_.ProbeAndLatch(frame_->Data() + start, bytes);
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        lcd_->Scan(scan_);
        std::memset(dib, 0, static_cast<size_t>(host_w) * host_h * 4u);
        const uint32_t columns = ScanColumns();
        const uint32_t lines   = std::min(ScanLines(), host_w);
        line_.resize(columns);
        for (uint32_t l = 0; l < lines; ++l) {
            scanout_->DecodeLine(scan_, l, columns, line_.data());
            for (uint32_t c = 0; c < columns; ++c) {
                const uint32_t py = columns - 1u - c;
                if (py >= host_h) continue;
                const uint32_t gray = lcd_pixel::Expand6(kLevelMax - NecVrc4171aLcd::PaletteGreen(line_[c]));
                dib[static_cast<size_t>(py) * host_w + l] = lcd_pixel::PackXrgb(gray, gray, gray);
            }
        }
    }

private:
    uint32_t ScanColumns() const { return std::min(scan_.width, glass_.height); }
    uint32_t ScanLines() const { return std::min(scan_.height, glass_.width); }

    NecVrc4171aLcd*                      lcd_   = nullptr;
    NecVrc4171aFrameMemory*              frame_ = nullptr;
    Vr41xxGiu*                           giu_   = nullptr;
    std::optional<NecVrc4171aLcdScanout> scanout_;
    DbLcdPanelSize                       glass_{};
    NecVrc4171aLcdScan                   scan_;
    std::vector<uint32_t>                line_;
};

}

REGISTER_SERVICE_AS(CompaqAero1500Renderer, PanelFrameRenderer);

}
