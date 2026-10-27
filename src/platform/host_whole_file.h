#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

namespace host_platform {

bool ReadWholeHostFile(const std::string& path, std::vector<uint8_t>& out);

}

}
