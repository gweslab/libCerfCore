#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2_emmc {

inline constexpr uint32_t kPdcfsSectorsPerCluster = 1u;
inline constexpr uint32_t kPdcfsReservedSectors   = 32u;
inline constexpr uint32_t kPdcfsFatCount          = 2u;

uint32_t PdcfsFatSectors(uint32_t volume_sectors);

bool FormatPdcfsVolume(std::vector<uint8_t>& data, uint32_t volume_sectors, std::string& error);

}

}
