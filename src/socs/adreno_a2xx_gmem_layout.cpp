#include "adreno_a2xx_gmem_layout.h"

namespace cerfcore {

AdrenoA2xxGmemLayout::AdrenoA2xxGmemLayout(uint32_t pitch, uint32_t bytes)
    : pitch_(pitch), bytes_(bytes) {}

uint64_t AdrenoA2xxGmemLayout::TileBytes() const {
    return uint64_t(kAdrenoA2xxGmemTileSide) * kAdrenoA2xxGmemTileSide * bytes_;
}

uint64_t AdrenoA2xxGmemLayout::TileRowBytes() const {
    return uint64_t(pitch_ / kAdrenoA2xxGmemTileSide) * TileBytes();
}

uint64_t AdrenoA2xxGmemLayout::Offset(uint32_t x, uint32_t y) const {
    const uint64_t tile = uint64_t(y / kAdrenoA2xxGmemTileSide) * (pitch_ / kAdrenoA2xxGmemTileSide) +
                          x / kAdrenoA2xxGmemTileSide;
    const uint64_t inside = uint64_t(y % kAdrenoA2xxGmemTileSide) * kAdrenoA2xxGmemTileSide +
                            x % kAdrenoA2xxGmemTileSide;
    return tile * TileBytes() + inside * bytes_;
}

uint32_t AdrenoA2xxGmemLayout::RunPixels(uint32_t x) const {
    return kAdrenoA2xxGmemTileSide - x % kAdrenoA2xxGmemTileSide;
}

uint64_t AdrenoA2xxGmemLayout::RowsBegin(uint32_t top) const {
    return uint64_t(top / kAdrenoA2xxGmemTileSide) * TileRowBytes();
}

uint64_t AdrenoA2xxGmemLayout::RowsEnd(uint32_t bottom) const {
    return (uint64_t((bottom - 1u) / kAdrenoA2xxGmemTileSide) + 1u) * TileRowBytes();
}

bool AdrenoA2xxGmemLayout::RowsOverlap(uint64_t a_base, const AdrenoA2xxGmemLayout& a, uint64_t b_base,
                                       const AdrenoA2xxGmemLayout& b, uint32_t top, uint32_t bottom) {
    return a_base + a.RowsEnd(bottom) > b_base + b.RowsBegin(top) &&
           b_base + b.RowsEnd(bottom) > a_base + a.RowsBegin(top);
}

}
