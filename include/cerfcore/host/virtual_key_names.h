#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace cerfcore {

std::optional<uint8_t> VirtualKeyByName(std::string_view name);

}
