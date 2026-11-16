#include "siemens_simatic_mobile_panels_gen2_factory_layout.h"

#include "siemens_simatic_mobile_panels_gen2_hardware_info.h"

#include <cerfcore/core/crc32.h>

#include <algorithm>
#include <cstring>
#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2_emmc {

void EnsureFactoryLayout(std::vector<uint8_t>& region, const std::array<uint8_t, 6>& hardware_mac,
                         SiemensSimaticMobilePanelsGen2OpType op_type, SiemensSimaticMobilePanelsGen2Panel panel) {
    constexpr uint32_t kSectorMagic = 0x20100305u;
    constexpr uint32_t kBootStateOk = 0x96A50001u;
    const std::vector<uint8_t> ktp400_oms_root = BuildSiemensSimaticMobilePanelsGen2HardwareInfoOms(hardware_mac, op_type, panel);
    const uint32_t kHwfSize = static_cast<uint32_t>(ktp400_oms_root.size());

    region.resize(kFactoryRegionBytes);
    const auto at = [&](uint32_t card_off) { return region.data() + (card_off - kFactoryTableOff); };
    const auto seed_at = [&](uint32_t table_off, uint32_t hwf_off, uint32_t boot_state_off, uint32_t pa_header_off,
                             uint32_t fwf_info_off) {
        uint8_t* table = at(table_off);
        std::memset(table, 0, 512u);
        cerf::le::Put32(table + 0x00u, kSectorMagic);
        cerf::le::Put32(table + 0x04u, 512u);
        cerf::le::Put32(table + 0x08u, 0u);
        cerf::le::Put32(table + 0x38u, boot_state_off);
        cerf::le::Put32(table + 0x3Cu, kBootStateSize);
        cerf::le::Put32(table + 0x40u, hwf_off);
        cerf::le::Put32(table + 0x44u, kHwfAreaSize);
        cerf::le::Put32(table + 0x48u, pa_header_off);
        cerf::le::Put32(table + 0x4Cu, kPaHeaderSize);
        cerf::le::Put32(table + 0x50u, fwf_info_off);
        cerf::le::Put32(table + 0x54u, kFlashbootSize);
        cerf::le::Put32(table + 0x70u, kDataPartitionOff);

        uint8_t* boot = at(boot_state_off);
        if (cerf::le::U32(boot) == 0u) cerf::le::Put32(boot, kBootStateOk);

        uint8_t* pa = at(pa_header_off);
        if (!(cerf::le::U32(pa + 0x00u) == 0x44484150u && cerf::le::U32(pa + 0x04u) == 1u)) {
            std::memset(pa, 0, kPaHeaderSize);
            cerf::le::Put32(pa + 0x00u, 0x44484150u);
            cerf::le::Put32(pa + 0x04u, 1u);
        }

        uint8_t* hwf = at(hwf_off);
        std::memset(hwf, 0, kHwfAreaSize);
        cerf::le::Put32(hwf + 0x00u, kHwfSize);
        std::memcpy(hwf + 0x08u, ktp400_oms_root.data(), ktp400_oms_root.size());
        cerf::le::Put32(hwf + 0x04u, Crc32::Update(0u, hwf + 0x08u, kHwfSize));
    };

    seed_at(kFactoryTableOff, kHwfOff, kBootStateOff, kPaHeaderOff, kFlashbootOff);
}

}

}
