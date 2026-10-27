#pragma once

#include <cstddef>
#include <string_view>

namespace cerfcore {

template <typename Char>
bool ReadCliOption(const Char* argument, std::string_view option, const Char*& value) {
    for (std::size_t i = 0; i < option.size(); ++i)
        if (argument[i] != Char(option[i])) return false;
    value = argument + option.size();
    return true;
}

std::string_view CliOptionName(std::string_view option);

}
