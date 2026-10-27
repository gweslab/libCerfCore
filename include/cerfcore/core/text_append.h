#pragma once

#include <cstddef>

namespace cerfcore {

template <typename Char>
constexpr bool AppendText(Char* buffer, std::size_t capacity, const Char* tail) {
    std::size_t used = 0;
    while (buffer[used] != Char()) ++used;
    std::size_t length = 0;
    while (tail[length] != Char()) ++length;
    if (used + length >= capacity) return false;
    while (*tail != Char()) buffer[used++] = *tail++;
    buffer[used] = Char();
    return true;
}

template <typename Char, std::size_t N>
constexpr bool AppendText(Char (&buffer)[N], const Char* tail) {
    return AppendText(&buffer[0], N, tail);
}

}
