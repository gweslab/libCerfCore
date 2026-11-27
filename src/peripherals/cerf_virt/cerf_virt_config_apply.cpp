#include <cerfcore/peripherals/cerf_virt/cerf_virt_config_apply.h>

#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_resize.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/emulation_freeze.h>

namespace cerfcore {

REGISTER_SERVICE(CerfVirtConfigApply);

bool CerfVirtConfigApply::ShouldRegister() { return emu_.Get<DeviceConfig>().guest_additions; }

void CerfVirtConfigApply::Apply(GuestReset reset, uint32_t prev_w, uint32_t prev_h) {
    const auto     live   = emu_.Get<DeviceConfig>().Live();
    const uint32_t w      = live->board_configurable_screen_width;
    const uint32_t h      = live->board_configurable_screen_height;
    auto           frozen = emu_.Get<EmulationFreeze>().WorkerSection();
    switch (reset) {
        case GuestReset::kSoft:
            emu_.Get<CerfVirtFramebuffer>().ShowHostMode(w, h);
            emu_.Get<GuestCpuReset>().WarmReset();
            return;
        case GuestReset::kHard:
            emu_.Get<CerfVirtFramebuffer>().ShowHostMode(w, h);
            emu_.Get<GuestColdBoot>().RequestHardReset();
            return;
        case GuestReset::kNone:
            if (w == prev_w && h == prev_h) return;
            if (!emu_.Get<CerfVirtFramebuffer>().FitsPrimary(w, h, "live customization")) return;
            emu_.Get<HostPresentation>().HostModeSet(w, h);
            emu_.Get<CerfVirtResize>().RequestResize(w, h);
            return;
    }
}

}
