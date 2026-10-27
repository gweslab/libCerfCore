#pragma once

#include <cstddef>

namespace cerfcore {

template <typename Char>
inline constexpr Char kPosixPathSeparators[] = {Char('/'), Char()};

template <typename Char>
inline constexpr Char kWindowsPathSeparators[] = {Char('\\'), Char('/'), Char()};

template <typename Char>
constexpr bool IsPathSeparator(Char c, const Char* separators) {
    for (const Char* separator = separators; *separator != Char(); ++separator)
        if (c == *separator) return true;
    return false;
}

template <typename Char>
constexpr std::size_t PathNameOffset(const Char* path, std::size_t length, const Char* separators) {
    std::size_t offset = 0;
    for (std::size_t i = 0; i < length; ++i)
        if (IsPathSeparator(path[i], separators)) offset = i + 1;
    return offset;
}

template <typename Char>
constexpr std::size_t PathParentLength(const Char* path, std::size_t length, const Char* separators) {
    const std::size_t offset = PathNameOffset(path, length, separators);
    return offset > 1 ? offset - 1 : offset;
}

template <typename String>
String ParentPath(const String& path, const typename String::value_type* separators) {
    const std::size_t length = PathParentLength(path.data(), path.size(), separators);
    return length == 0 ? String(1, typename String::value_type('.')) : path.substr(0, length);
}

template <typename String>
void TrimTrailingSeparators(String& path, const typename String::value_type* separators, std::size_t keep) {
    while (path.size() > keep && IsPathSeparator(path.back(), separators)) path.pop_back();
}

template <typename String>
String AppendPathName(const String& folder, const String& name, const typename String::value_type* separators) {
    if (folder.empty() || IsPathSeparator(folder.back(), separators)) return folder + name;
    return folder + separators[0] + name;
}

}
