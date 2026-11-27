#pragma once
#include <cstdint>

namespace cerfcore {

inline constexpr uint32_t kAdrenoA2xxGmemTileSide = 32u;

class AdrenoA2xxGmemLayout {
public:
    AdrenoA2xxGmemLayout(uint32_t pitch, uint32_t bytes);

    uint64_t Offset(uint32_t x, uint32_t y) const;
    uint32_t RunPixels(uint32_t x) const;
    uint64_t RowsBegin(uint32_t top) const;
    uint64_t RowsEnd(uint32_t bottom) const;

    static bool RowsOverlap(uint64_t a_base, const AdrenoA2xxGmemLayout& a, uint64_t b_base,
                            const AdrenoA2xxGmemLayout& b, uint32_t top, uint32_t bottom);

private:
    uint64_t TileBytes() const;
    uint64_t TileRowBytes() const;

    uint32_t pitch_;
    uint32_t bytes_;
};

}
