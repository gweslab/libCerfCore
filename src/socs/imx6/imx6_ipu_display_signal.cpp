#include "imx6_ipu_display_signal.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_ipu.h"
#include "imx6_ipu_cpmem.h"
#include "imx6_ipu_register_map.h"
#include "imx6_id.h"

namespace cerfcore {

bool Imx6IpuDisplaySignal::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocId::IsImx6(board->GetSocId());
}

void Imx6IpuDisplaySignal::Refresh() {
    auto& ipu = emu_.Get<Imx6Ipu>();
    auto& memory = emu_.Get<Imx6IpuCpmem>();
    bool enabled = false;
    for (const imx6_ipu::DisplayChannel& display_channel : imx6_ipu::kDisplayChannels) {
        if (display_channel.role != imx6_ipu::DisplayChannelRole::MainPlane) continue;
        const uint32_t channel = display_channel.number;
        if (!ipu.IdmacChannelEnabled(channel) || !ipu.IdmacChannelReady(channel)) continue;
        const auto desc = memory.DecodeChannel(channel);
        if (!desc.valid) continue;
        const uint32_t dc_conf_offset = channel == 23u   ? imx6_ipu::kOffDcWrChConf5
                                        : channel == 24u ? imx6_ipu::kOffDcWrChConf6
                                                         : imx6_ipu::kOffDcWrChConf1;
        const uint32_t channel_mode = (ipu.RegisterValue(dc_conf_offset) >> 5) & 0x7u;
        if (channel_mode == 0u) continue;
        ValidateTemplates();
        active_display_channel_ = channel;
        enabled = true;
        break;
    }
    if (enabled && !panel_enabled_) emu_.Get<HostPresentation>().PanelEnabled();
    panel_enabled_ = enabled;
    if (!enabled) active_display_channel_ = 0xFFFFFFFFu;
}

bool Imx6IpuDisplaySignal::PanelEnabled() const { return panel_enabled_; }

uint32_t Imx6IpuDisplaySignal::ActiveDisplayChannel() const { return active_display_channel_; }

void Imx6IpuDisplaySignal::Reset() {
    panel_enabled_ = false;
    active_display_channel_ = 0xFFFFFFFFu;
}

void Imx6IpuDisplaySignal::ResetActiveDisplayChannel() { active_display_channel_ = 0xFFFFFFFFu; }

void Imx6IpuDisplaySignal::SaveState(StateWriter& writer) {
    writer.Write<uint8_t>("panel_enabled", panel_enabled_ ? 1u : 0u);
    writer.Write("active_display_channel", active_display_channel_);
}

void Imx6IpuDisplaySignal::RestoreState(StateReader& reader) {
    uint8_t panel_enabled = 0u;
    reader.Read("panel_enabled", panel_enabled);
    panel_enabled_ = panel_enabled != 0u;
    reader.Read("active_display_channel", active_display_channel_);
}

void Imx6IpuDisplaySignal::ValidateTemplates() {
    auto& memory = emu_.Get<Imx6IpuCpmem>();
    const uint32_t template_base = memory.MmioBase() + 0x80000u;
    for (uint32_t word = 0; word < 256u; ++word) {
        const uint32_t mapping_field = (memory.ReadWord(template_base + word * 8u) >> 15) & 0x1Fu;
        if (mapping_field != 0u) ValidateMapping(mapping_field - 1u);
    }
}

void Imx6IpuDisplaySignal::ValidateMapping(uint32_t mapping) const {
    if (mapping > 29u)
        emu_.Get<Fatal>().Die("[IPU DC] DC template mapping index %u is outside DC_MAP_CONF_0..14", mapping);
    const auto& ipu = emu_.Get<Imx6Ipu>();
    const uint32_t pointer_offset = imx6_ipu::kOffDcMapConf0 + 4u * (mapping / 2u);
    const uint32_t pointers = (ipu.RegisterValue(pointer_offset) >> ((mapping & 1u) * 16u)) & 0x7FFFu;
    const uint32_t slots[] = {pointers & 0x1Fu, (pointers >> 5) & 0x1Fu, (pointers >> 10) & 0x1Fu};
    uint32_t descriptors[3] = {};
    bool valid = true;
    for (uint32_t component = 0; component < 3u; ++component) {
        const uint32_t slot = slots[component];
        if (slot > 23u)
            emu_.Get<Fatal>().Die("[IPU DC] MAPPING_PNTR_BYTE%u_%u selects slot %u outside DC_MAP_CONF_15..26",
                                  component, mapping, slot);
        const uint32_t descriptor = descriptors[component] =
            (ipu.RegisterValue(imx6_ipu::kOffDcMapConf15 + 4u * (slot / 2u)) >> ((slot & 1u) * 16u)) &
            0xFFFFu;
        const uint32_t expected_offset = 7u + component * 8u;
        if (descriptor != ((expected_offset << 8) | 0xFFu)) {
            valid = false;
            continue;
        }
    }
    if (!valid)
        emu_.Get<Fatal>().Die(
            "[IPU DC] DC_MAP_CONF_%u mapping is not RGB888 identity (ptr=0x%04X slots=%u,%u,%u "
            "descriptors=0x%04X,0x%04X,0x%04X conf1=0x%08X conf5=0x%08X conf6=0x%08X)",
            mapping, pointers, slots[0], slots[1], slots[2], descriptors[0], descriptors[1], descriptors[2],
            ipu.RegisterValue(imx6_ipu::kOffDcWrChConf1), ipu.RegisterValue(imx6_ipu::kOffDcWrChConf5),
            ipu.RegisterValue(imx6_ipu::kOffDcWrChConf6));
}

REGISTER_SERVICE(Imx6IpuDisplaySignal);

}
