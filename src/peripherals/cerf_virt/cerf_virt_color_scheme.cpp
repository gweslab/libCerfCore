#include "cerf_virt_color_scheme.h"
#include <cerfcore/peripherals/cerf_virt/cerf_virt_color_scheme_regs.h>
#include <cerfcore/peripherals/cerf_virt/guest_color_schemes.h>

#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <string>

namespace cerfcore {

REGISTER_SERVICE(CerfVirtColorScheme);

bool CerfVirtColorScheme::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtColorScheme::OnReady() {
    ApplyFromConfig();
    emu_.Get<GuestCpuReset>().RegisterResetListener(
        [this](ResetLineKind) { ApplyFromConfig(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void CerfVirtColorScheme::ApplyFromConfig() {
    const std::string& key = emu_.Get<DeviceConfig>().guest_additions_color_scheme;
    count_   = 0;
    present_ = false;
    if (key.empty()) return;

    const GuestColorScheme* scheme = FindGuestColorScheme(key);
    if (!scheme) {
        LOG(GuestAdditions, "unknown --ga-color-scheme key '%s'", key.c_str());
        CerfFatalExit();
    }
    count_ = scheme->count;
    for (uint32_t i = 0; i < count_ && i < CerfVirt::kColorSchemeMax; ++i)
        entries_[i] = scheme->entries[i];
    present_ = true;
    LOG(GuestAdditions, "color scheme override: '%s' published (%u entries)",
        key.c_str(), count_);
}

uint32_t CerfVirtColorScheme::MmioBase() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() + CerfVirt::kColorSchemeOffset;
}
uint32_t CerfVirtColorScheme::MmioSize() const { return CerfVirt::kColorSchemeSize; }

uint32_t CerfVirtColorScheme::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off == CerfVirt::kCscPresent) return present_ ? CerfVirt::kCscMagic : 0u;
    if (off == CerfVirt::kCscCount)   return present_ ? count_ : 0u;
    if (off >= CerfVirt::kCscEntries) {
        const uint32_t idx = (off - CerfVirt::kCscEntries) >> 2u;
        if (idx < CerfVirt::kColorSchemeMax) return entries_[idx];
    }
    return 0u;
}

void CerfVirtColorScheme::WriteWord(uint32_t, uint32_t) {}

void CerfVirtColorScheme::SaveState(StateWriter& w) {
    w.Write<uint32_t>("present", present_ ? 1u : 0u);
    w.Write<uint32_t>("count", count_);
    for (uint32_t i = 0; i < CerfVirt::kColorSchemeMax; ++i) w.Write<uint32_t>("entries", entries_[i]);
}

void CerfVirtColorScheme::RestoreState(StateReader& r) {
    uint32_t v;
    r.Read("present", v); present_ = (v != 0u);
    r.Read("count", v); count_ = v;
    for (uint32_t i = 0; i < CerfVirt::kColorSchemeMax; ++i) { r.Read("entries", v); entries_[i] = v; }
}

}
