#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace cerfcore {

struct GuestColorScheme {
    std::string_view key;
    std::string_view label;
    const uint32_t*  entries;
    uint32_t         count;
};

std::span<const GuestColorScheme> GuestColorSchemes();
const GuestColorScheme*           FindGuestColorScheme(std::string_view key);

}
