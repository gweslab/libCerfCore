#include "siemens_simatic_mobile_panels_gen2_boot_handoff.h"
#include "siemens_simatic_mobile_panels_gen2_oat.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/crc32.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../net/network_backend.h"
#include "siemens_simatic_mobile_panels_gen2_hardware_info.h"
#include "../../socs/imx6/imx6_fec.h"

#include <vector>
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kHwInfoHandoffPa = 0x10005000u;

}

bool SiemensSimaticMobilePanelsGen2BootHandoff::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
}

void SiemensSimaticMobilePanelsGen2BootHandoff::Place(const SiemensSimaticMobilePanelsGen2OalLayout& oal) {
    auto& mem = emu_.Get<EmulatedMemory>();

    const std::vector<uint8_t> oms_root =
        BuildSiemensSimaticMobilePanelsGen2HardwareInfoOms(
            emu_.Get<NetworkBackend>().MacForReceiver(kImx6FecReceiverId, NetworkBackend::ReceiverKind::Ethernet),
            oal.op_type, oal.panel);
    const uint32_t hwf_size = static_cast<uint32_t>(oms_root.size());

    mem.WriteWord(kHwInfoHandoffPa + 0x00u, hwf_size);
    mem.WriteWord(kHwInfoHandoffPa + 0x04u, Crc32::Update(0u, oms_root.data(), oms_root.size()));
    for (uint32_t i = 0; i < hwf_size; ++i)
        mem.WriteByte(kHwInfoHandoffPa + 0x08u + i, oms_root[i]);

    LOG(Boot, "%s: MicroOMS hardware info of %u bytes placed at PA 0x%08X\n", oal.log_tag, hwf_size,
        kHwInfoHandoffPa);
}

REGISTER_SERVICE(SiemensSimaticMobilePanelsGen2BootHandoff);

}
