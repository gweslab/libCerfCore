#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

enum class HostFileRead : uint8_t {
    kRead,
    kAbsent,
    kUnreadable,
};

HostFileRead ReadHostFileIfPresent(const std::string& path, std::vector<uint8_t>& out);

}
