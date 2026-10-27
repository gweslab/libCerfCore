#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <utility>

namespace cerfcore {

template <class T, size_t N>
T ValueByName(const std::pair<std::string_view, T> (&table)[N], std::string_view name) {
    return std::find_if(std::begin(table), std::end(table), [name](const auto& entry) {
        return entry.first == name;
    })->second;
}

}
