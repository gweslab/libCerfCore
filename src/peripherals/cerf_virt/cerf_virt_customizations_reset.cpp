#include <cerfcore/peripherals/cerf_virt/cerf_virt_customizations_reset.h>

#include "cerf_virt_color_scheme.h"
#include "cerf_virt_system_font.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/socs/guest_cpu_reset.h>

namespace cerfcore {

REGISTER_SERVICE(CerfVirtCustomizationsReset);

bool CerfVirtCustomizationsReset::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtCustomizationsReset::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        const GuestCpuReset& reset = emu_.Get<GuestCpuReset>();
        if (reset.DeliveredResetWasResume() || !reset.PowerOnReset(kind)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        registry_known_ = false;
    });
}

CerfVirtCustomizationsReset::RegistryCustomizations CerfVirtCustomizationsReset::Published() {
    RegistryCustomizations     out;
    const CerfVirtColorScheme& scheme = emu_.Get<CerfVirtColorScheme>();
    out.scheme_count                  = scheme.PublishedCount();
    for (uint32_t i = 0; i < out.scheme_count; ++i) out.scheme_entries[i] = scheme.PublishedEntries()[i];
    const CerfVirtSystemFont& font = emu_.Get<CerfVirtSystemFont>();
    out.font_set                   = font.Set() ? 1u : 0u;
    out.font_height                = out.font_set != 0u ? font.Height() : 0;
    return out;
}

void CerfVirtCustomizationsReset::OnCustomizationsApplied() {
    const RegistryCustomizations published = Published();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (registry_known_ && registry_ == published) return;
        registry_known_ = true;
        registry_       = published;
    }
    LOG(GuestAdditions,
        "registry customizations committed - soft-resetting the guest so GWES "
        "re-reads them\n");
    emu_.Get<HostPresentation>().CustomizationsReset();
    emu_.Get<GuestCpuReset>().WarmReset();
}

void CerfVirtCustomizationsReset::SaveState(StateWriter& w) const {
    std::lock_guard<std::mutex> lock(mutex_);
    w.Write<uint32_t>("registry_known", registry_known_ ? 1u : 0u);
    w.Write<uint32_t>("registry_scheme_count", registry_.scheme_count);
    for (uint32_t entry : registry_.scheme_entries) w.Write<uint32_t>("registry_scheme_entry", entry);
    w.Write<uint32_t>("registry_font_set", registry_.font_set);
    w.Write<int32_t>("registry_font_height", registry_.font_height);
}

void CerfVirtCustomizationsReset::RestoreState(StateReader& r) {
    uint32_t               known = 0;
    RegistryCustomizations saved;
    r.Read("registry_known", known);
    r.Read("registry_scheme_count", saved.scheme_count);
    for (uint32_t& entry : saved.scheme_entries) r.Read("registry_scheme_entry", entry);
    r.Read("registry_font_set", saved.font_set);
    r.Read("registry_font_height", saved.font_height);
    std::lock_guard<std::mutex> lock(mutex_);
    registry_known_ = known != 0u;
    registry_       = saved;
}

}
