#pragma once

#include "siemens_simatic_mobile_panels_gen2_hardware_info.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2_emmc {

inline constexpr uint32_t kFactoryTableOff = 0x00101000u;
inline constexpr uint32_t kHwfOff = 0x00102000u;
inline constexpr uint32_t kHwfAreaSize = 0x00019200u;
inline constexpr uint32_t kBootStateOff = kHwfOff + kHwfAreaSize;
inline constexpr uint32_t kBootStateSize = 0x00000200u;
inline constexpr uint32_t kPaHeaderOff = kBootStateOff + kBootStateSize;
inline constexpr uint32_t kPaHeaderSize = 0x00000800u;
inline constexpr uint32_t kFactoryRegionBytes = kPaHeaderOff + kPaHeaderSize - kFactoryTableOff;
inline constexpr uint32_t kFlashbootOff = 0x02000000u;
inline constexpr uint32_t kFlashbootSize = 0x04000000u;
inline constexpr uint32_t kDataPartitionOff = kFlashbootOff + kFlashbootSize;
inline constexpr uint32_t kDataPartitionSize = 0x20000000u;

void EnsureFactoryLayout(std::vector<uint8_t>& region, const std::array<uint8_t, 6>& hardware_mac,
                         SiemensSimaticMobilePanelsGen2OpType op_type, SiemensSimaticMobilePanelsGen2Panel panel);

}

}
