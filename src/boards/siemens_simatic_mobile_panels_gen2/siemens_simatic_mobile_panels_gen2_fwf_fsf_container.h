#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2_fwf {

struct FsfEntry {
    std::string dir;
    std::string name;
    std::vector<uint8_t> data;
};

bool ParseFsfVolume(const std::vector<uint8_t>& fwf, std::vector<FsfEntry>& out, std::string& error);

}

}
