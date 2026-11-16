#pragma once

#include "siemens_simatic_mobile_panels_gen2_factory_layout.h"
#include "siemens_simatic_mobile_panels_gen2_hardware_info.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

class DiskImage;

namespace siemens_simatic_mobile_panels_gen2_emmc {

inline constexpr uint32_t kPartitionLba     = kDataPartitionOff / 512u;
inline constexpr uint32_t kPartitionSectors = kDataPartitionSize / 512u;
inline constexpr uint32_t kCardSectors      = kPartitionLba + kPartitionSectors;

struct CardInputs {
    std::vector<uint8_t>   fwf_container;
    std::array<uint8_t, 6> hardware_mac{};
    SiemensSimaticMobilePanelsGen2OpType        op_type{};
    SiemensSimaticMobilePanelsGen2Panel         panel{};
};

bool ReadCardRange(DiskImage& image, uint64_t offset, size_t size, std::vector<uint8_t>& out);
bool WriteCardRange(DiskImage& image, uint64_t offset, const uint8_t* data, size_t size);

bool CardLayoutValid(DiskImage& image);
bool RefreshFactoryArea(DiskImage& image, const CardInputs& inputs);
bool SeedCard(DiskImage& image, const CardInputs& inputs, std::string& error);

}

}
