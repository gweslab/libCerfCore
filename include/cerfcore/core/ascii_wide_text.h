#pragma once

#include <cstddef>

namespace cerfcore {

template <std::size_t N>
struct AsciiWideText {
    wchar_t text[N];
    constexpr explicit AsciiWideText(const char (&ascii)[N]) : text() {
        for (std::size_t i = 0; i < N; ++i) text[i] = wchar_t(ascii[i]);
    }
};

}
