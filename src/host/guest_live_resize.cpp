#include <cerfcore/host/guest_live_resize.h>

#include "../boot/guest_xip_image.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/user_config_writer.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_resize.h>
#include <cerfcore/state/emulation_freeze.h>

namespace cerfcore {

REGISTER_SERVICE(GuestLiveResize);

bool GuestLiveResize::ShouldRegister() { return emu_.Get<DeviceConfig>().guest_additions; }

bool GuestLiveResize::Supported() const {
    if (emu_.Get<CerfVirtResize>().GuestFixedSize()) return false;
    uint16_t major = 0, minor = 0;
    auto*    image = emu_.TryGet<GuestXipImage>();
    if (!image || !image->KernelSubsystemVersion(major, minor)) return true;
    return major > 3;
}

void GuestLiveResize::Request(uint32_t surface_w, uint32_t surface_h) {
    if (surface_w == 0 || surface_h == 0) return;
    auto&          presentation = emu_.Get<HostPresentation>();
    const uint32_t current_w    = presentation.SurfaceWidth();
    const uint32_t current_h    = presentation.SurfaceHeight();
    LOG(Lcd, "GuestLiveResize: surface %ux%u requested, current %ux%u\n", surface_w, surface_h, current_w,
        current_h);
    const bool pending = last_w_ != 0 && (last_w_ != current_w || last_h_ != current_h);
    if (pending ? (surface_w == last_w_ && surface_h == last_h_) : (surface_w == current_w && surface_h == current_h))
        return;
    if (!emu_.Get<CerfVirtFramebuffer>().FitsPrimary(surface_w, surface_h, "live resize")) return;
    last_w_ = surface_w;
    last_h_ = surface_h;
    {
        auto frozen = emu_.Get<EmulationFreeze>().WorkerSection();
        emu_.Get<CerfVirtResize>().RequestResize(surface_w, surface_h);
    }
    emu_.Get<UserConfigWriter>().WriteConfigurableScreenSize(surface_w, surface_h);
}

}
