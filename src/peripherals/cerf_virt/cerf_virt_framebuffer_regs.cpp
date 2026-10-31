#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_customizations_reset.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_fb_regs.h>

#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/refresh_rate_service.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

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

class CerfVirtFramebufferRegs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        fb_ = &emu_.Get<CerfVirtFramebuffer>();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (!emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) refresh_hz_ = 0u;
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    void SaveState(StateWriter& w) override { w.Write("guest_refresh_rate", refresh_hz_); }
    void RestoreState(StateReader& r) override { r.Read("guest_refresh_rate", refresh_hz_); }

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
            case kFbRegLogicalDpi: return emu_.Get<DeviceConfig>().screen_dpi;
            case kFbRegRefreshRate: return GuestRefreshRate();
            case kFbRegSystemFontHeight:
                return (uint32_t)emu_.Get<DeviceConfig>().guest_additions_font_size;
            case kFbRegSystemFontPresent:
                return emu_.Get<DeviceConfig>().guest_additions_font_size_set ? 1u : 0u;
            case kFbRegClearType:
                return ClearTypeMode();
            case kFbRegAllowUntrustedApps:
                return emu_.Get<DeviceConfig>().guest_additions_allow_untrusted_apps ? 1u : 0u;
            default:
                emu_.Get<Fatal>().Die("[CerfVirtFbRegs] read +0x%X", off);
        }
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == kFbRegScanoutOffset) { fb_->SetScanoutOffset(value); return; }
        if (off == kFbRegCustomizationsApplied) {
            emu_.Get<CerfVirtCustomizationsReset>().OnCustomizationsApplied();
            return;
        }
        emu_.Get<Fatal>().Die("[CerfVirtFbRegs] write +0x%X = 0x%08X", off, value);
    }

private:
    uint32_t GuestRefreshRate() {
        if (refresh_hz_ == 0u)
            refresh_hz_ = static_cast<uint32_t>(emu_.Get<RefreshRateService>().GetRefreshRate());
        return refresh_hz_;
    }

    uint32_t ClearTypeMode() const {
        const auto& cfg = emu_.Get<DeviceConfig>();
        if (!cfg.guest_additions_cleartype_set) return CerfVirt::kFbClearTypeDefault;
        return cfg.guest_additions_cleartype ? CerfVirt::kFbClearTypeOn
                                             : CerfVirt::kFbClearTypeOff;
    }

    CerfVirtFramebuffer* fb_         = nullptr;
    uint32_t             refresh_hz_ = 0;
};

REGISTER_SERVICE(CerfVirtFramebufferRegs);

}

}
