#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_customizations_reset.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_fb_regs.h>

#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripheral_dispatcher.h"
#include "cerf_virt_overlay_plane.h"
#include "cerf_virt_system_font.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/hw_screen.h>
#include <cerfcore/host/refresh_rate_service.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstdio>

namespace cerfcore {

namespace {

constexpr char kGpuBudgetNotice[] =
    "Guest additions run at %ux%u. The graphics memory of the guest does not hold the screen at %ux%u.";

using CerfVirt::kFbRegWidth;
using CerfVirt::kFbRegHeight;
using CerfVirt::kFbRegBpp;
using CerfVirt::kFbRegStride;
using CerfVirt::kFbRegMemBasePa;
using CerfVirt::kFbRegMemSizeTotal;
using CerfVirt::kFbRegPrimaryReserve;
using CerfVirt::kFbRegLogicalDpi;
using CerfVirt::kFbRegRefreshRate;
using CerfVirt::kFbRegSystemFontHeight;
using CerfVirt::kFbRegSystemFontPresent;
using CerfVirt::kFbRegCustomizationsApplied;
using CerfVirt::kFbRegClearType;
using CerfVirt::kFbRegAllowUntrustedApps;
using CerfVirt::kFbRegScanoutOffset;
using CerfVirt::kFbRegLayerWidth;
using CerfVirt::kFbRegLayerHeight;
using CerfVirt::kFbRegLayerStride;
using CerfVirt::kFbRegLayerBpp;
using CerfVirt::kFbRegLayerPa;

class CerfVirtFramebufferRegs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        fb_   = &emu_.Get<CerfVirtFramebuffer>();
        font_ = &emu_.Get<CerfVirtSystemFont>();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            refresh_hz_ = 0u;
            layer_      = CerfVirtLayer{};
            overlay_    = CerfVirtOverlay{};
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    void SaveState(StateWriter& w) override {
        w.Write("guest_refresh_rate", refresh_hz_);
        w.Write("layer_width", layer_.width);
        w.Write("layer_height", layer_.height);
        w.Write("layer_stride", layer_.stride);
        w.Write("layer_bpp", layer_.bpp);
        StateWriteField field(w);
        CerfVirtOverlay::Visit(overlay_, field);
        font_->SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        r.Read("guest_refresh_rate", refresh_hz_);
        r.Read("layer_width", layer_.width);
        r.Read("layer_height", layer_.height);
        r.Read("layer_stride", layer_.stride);
        r.Read("layer_bpp", layer_.bpp);
        StateReadField field(r);
        CerfVirtOverlay::Visit(overlay_, field);
        font_->RestoreState(r);
    }

    uint32_t MmioBase() const override {
        return emu_.Get<BoardContext>().GuestAdditionsWindowBase()
             + CerfVirt::kFramebufferRegsOffset;
    }
    uint32_t MmioSize() const override { return CerfVirt::kFramebufferRegsSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
            case kFbRegWidth:     return fb_->Width();
            case kFbRegHeight:    return fb_->Height();
            case kFbRegBpp:       return fb_->Bpp();
            case kFbRegStride:    return fb_->Stride();
            case kFbRegMemBasePa: return fb_->MemBasePa();
            case kFbRegMemSizeTotal: return fb_->RegionBytes();
            case kFbRegPrimaryReserve: return fb_->PrimaryReserveBytes();
            case kFbRegLogicalDpi: return emu_.Get<DeviceConfig>().Live()->screen_dpi;
            case kFbRegRefreshRate: return GuestRefreshRate();
            case kFbRegSystemFontHeight:
                return (uint32_t)font_->Height();
            case kFbRegSystemFontPresent:
                return font_->Set() ? 1u : 0u;
            case kFbRegClearType:
                return ClearTypeMode();
            case kFbRegAllowUntrustedApps:
                return emu_.Get<DeviceConfig>().Live()->guest_additions_allow_untrusted_apps ? 1u : 0u;
            case CerfVirt::kFbRegPanelWidth:  return Panel().width;
            case CerfVirt::kFbRegPanelHeight: return Panel().height;
            default:
                emu_.Get<Fatal>().Die("[CerfVirtFbRegs] read +0x%X", off);
        }
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
            case kFbRegScanoutOffset: fb_->SetScanoutOffset(value); return;
            case kFbRegLayerWidth:    layer_.width  = value; return;
            case kFbRegLayerHeight:   layer_.height = value; return;
            case kFbRegLayerStride:   layer_.stride = value; return;
            case kFbRegLayerBpp:      layer_.bpp    = value; return;
            case kFbRegLayerPa: {
                CerfVirtLayer layer = layer_;
                layer.pa = value;
                fb_->SetLayer(value != 0u ? layer : CerfVirtLayer{});
                return;
            }
            case CerfVirt::kFbRegOverlayFormat:       overlay_.format        = value; return;
            case CerfVirt::kFbRegOverlayWidth:        overlay_.width         = value; return;
            case CerfVirt::kFbRegOverlayHeight:       overlay_.height        = value; return;
            case CerfVirt::kFbRegOverlayPa:           overlay_.pa            = value; return;
            case CerfVirt::kFbRegOverlayStride:       overlay_.stride        = value; return;
            case CerfVirt::kFbRegOverlayCbOffset:     overlay_.cb_offset     = value; return;
            case CerfVirt::kFbRegOverlayCrOffset:     overlay_.cr_offset     = value; return;
            case CerfVirt::kFbRegOverlayChromaStride: overlay_.chroma_stride = value; return;
            case CerfVirt::kFbRegOverlaySrcLeft:      overlay_.src_left      = value; return;
            case CerfVirt::kFbRegOverlaySrcTop:       overlay_.src_top       = value; return;
            case CerfVirt::kFbRegOverlaySrcRight:     overlay_.src_right     = value; return;
            case CerfVirt::kFbRegOverlaySrcBottom:    overlay_.src_bottom    = value; return;
            case CerfVirt::kFbRegOverlayDstLeft:      overlay_.dst_left      = value; return;
            case CerfVirt::kFbRegOverlayDstTop:       overlay_.dst_top       = value; return;
            case CerfVirt::kFbRegOverlayDstRight:     overlay_.dst_right     = value; return;
            case CerfVirt::kFbRegOverlayDstBottom:    overlay_.dst_bottom    = value; return;
            case CerfVirt::kFbRegOverlayClockwise:    overlay_.clockwise     = value; return;
            case CerfVirt::kFbRegOverlaySelectX:      overlay_.select_x      = value; return;
            case CerfVirt::kFbRegOverlaySelectY:      overlay_.select_y      = value; return;
            case CerfVirt::kFbRegOverlayLayers: {
                overlay_.layers = value;
                emu_.Get<CerfVirtOverlayPlane>().Validate(overlay_);
                fb_->SetOverlay(overlay_);
                return;
            }
            case CerfVirt::kFbRegBacklightPercent: fb_->SetBacklightPercent(value); return;
            case CerfVirt::kFbRegGpuBudgetCap:
                if (value == 1u) {
                    CapToPanel();
                    return;
                }
                break;
        }
        if (off == kFbRegCustomizationsApplied) {
            emu_.Get<CerfVirtCustomizationsReset>().OnCustomizationsApplied();
            return;
        }
        emu_.Get<Fatal>().Die("[CerfVirtFbRegs] write +0x%X = 0x%08X", off, value);
    }

private:
    DbLcdPanelSize Panel() {
        const DbBoard* board = emu_.Get<BoardDatabase>().FindBoard(emu_.Get<DeviceConfig>().board_id);
        if (!board->lcd_panel_size)
            emu_.Get<Fatal>().Die("[CerfVirtFbRegs] the guest read the panel size of board %s, which declares none",
                                  board->id.c_str());
        return *board->lcd_panel_size;
    }

    void CapToPanel() {
        uint32_t w = 0, h = 0;
        fb_->Geometry(w, h);
        const DbLcdPanelSize panel = Panel();
        if (!fb_->SetHostMode(panel.width, panel.height))
            emu_.Get<Fatal>().Die("[CerfVirtFbRegs] the %ux%u panel mode does not fit the primary reserve",
                                  panel.width, panel.height);
        char msg[192];
        snprintf(msg, sizeof(msg), kGpuBudgetNotice, panel.width, panel.height, w, h);
        LOG(Caution, "[CerfVirtFbRegs] %s\n", msg);
        emu_.Get<HwScreen>().AddLine(msg);
    }

    uint32_t GuestRefreshRate() {
        if (refresh_hz_ == 0u)
            refresh_hz_ = static_cast<uint32_t>(emu_.Get<RefreshRateService>().GetRefreshRate());
        return refresh_hz_;
    }

    uint32_t ClearTypeMode() const {
        const auto live = emu_.Get<DeviceConfig>().Live();
        if (!live->guest_additions_cleartype_set) return CerfVirt::kFbClearTypeDefault;
        return live->guest_additions_cleartype ? CerfVirt::kFbClearTypeOn : CerfVirt::kFbClearTypeOff;
    }

    CerfVirtFramebuffer* fb_         = nullptr;
    CerfVirtSystemFont*  font_       = nullptr;
    uint32_t             refresh_hz_ = 0;
    CerfVirtLayer        layer_{};
    CerfVirtOverlay      overlay_{};
};

REGISTER_SERVICE(CerfVirtFramebufferRegs);

}

}
