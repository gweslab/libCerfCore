#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

class DiskImage;

namespace siemens_simatic_mobile_panels_gen2_flashboot {

struct BootImage {
    std::vector<uint8_t> flat;
    uint32_t base_va = 0;
    uint32_t entry_va = 0;
    std::string version;
    uint32_t stream_crc = 0;
};

bool HasBootRecipe(DiskImage& card, std::string& error);

bool VerifyBootImage(DiskImage& card, uint32_t& stream_crc, std::string& error);

bool ReadBootImage(DiskImage& card, BootImage& out, std::string& error);

bool ProgramFromFwf(DiskImage& card, const std::vector<uint8_t>& fwf, std::string& error);

bool ReadUpdateVolume(const std::vector<uint8_t>& fwf, std::vector<uint8_t>& volume, std::string& error);

}

}
