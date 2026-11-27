#include "cerf_virt_system_font.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

REGISTER_SERVICE(CerfVirtSystemFont);

bool CerfVirtSystemFont::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtSystemFont::OnReady() {
    LatchConfigured();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        LatchConfigured();
    });
}

void CerfVirtSystemFont::LatchConfigured() {
    const auto live = emu_.Get<DeviceConfig>().Live();
    set_.store(live->guest_additions_font_size_set, std::memory_order_release);
    height_.store(live->guest_additions_font_size, std::memory_order_release);
}

void CerfVirtSystemFont::SaveState(StateWriter& w) const {
    w.Write<uint8_t>("system_font_set", Set() ? 1u : 0u);
    w.Write<int32_t>("system_font_height", Height());
}

void CerfVirtSystemFont::RestoreState(StateReader& r) {
    uint8_t set    = 0;
    int32_t height = 0;
    r.Read("system_font_set", set);
    r.Read("system_font_height", height);
    set_.store(set != 0u, std::memory_order_release);
    height_.store(height, std::memory_order_release);
}

}
