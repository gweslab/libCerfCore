#pragma once

#include "path_split.h"

#include <cstddef>
#include <vector>

namespace cerfcore {

template <typename String>
std::vector<String> PathSegments(const String& path, std::size_t start,
                                 const typename String::value_type* separators) {
    std::vector<String> segments;
    std::size_t         i = start;
    while (i < path.size()) {
        while (i < path.size() && IsPathSeparator(path[i], separators)) ++i;
        const std::size_t begin = i;
        while (i < path.size() && !IsPathSeparator(path[i], separators)) ++i;
        if (i > begin) segments.push_back(path.substr(begin, i - begin));
    }
    return segments;
}

}
