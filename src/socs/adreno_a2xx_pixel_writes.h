#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace cerfcore {

struct AdrenoA2xxPixelWrite {
    uint8_t*               target;
    std::array<uint8_t, 4> data;
    uint32_t               bytes;
};

struct AdrenoA2xxPixelWrites {
    std::unique_ptr<AdrenoA2xxPixelWrite[]> pixels;
    size_t   size     = 0;
    size_t   capacity = 0;
    uint64_t written  = 0;

    void Push(const AdrenoA2xxPixelWrite& pixel) {
        if (size == capacity) Grow();
        pixels[size++] = pixel;
    }
    void Grow();
};

inline void AdrenoA2xxCopyPixelBytes(uint8_t* destination, const uint8_t* source, uint32_t bytes) {
    if (bytes == 4u) std::memcpy(destination, source, 4u);
    else if (bytes == 2u) std::memcpy(destination, source, 2u);
    else std::memcpy(destination, source, bytes);
}

}
