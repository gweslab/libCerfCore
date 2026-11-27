#include "adreno_a2xx_tiling.h"

namespace cerfcore {

/* Ford SYNC 2 librenderboy.dll: 0x41CE3440 (2D layout),
   0x41CE3A1C-0x41CE3ABC (CPU tiled upload/readback address). */
uint64_t AdrenoA2xxTiledAddress(uint32_t base, uint32_t pitch, uint32_t bytes,
    uint32_t x, uint32_t y) {
    const uint64_t macro = (uint64_t(pitch >> 5) * (y >> 5) + (x >> 5)) * (bytes << 7);
    const uint64_t micro = ((x & 7u) + ((y & 6u) << 2)) * bytes;
    const uint64_t offset = macro + ((y >> 3) & 1u) * (bytes << 6) +
        ((micro + ((y & 1u) << 3)) << 1) - (micro & 15u) + (base >> 3);
    const uint32_t bank = (((((y >> 2) & ~1u) + (x >> 3)) << 1) & 6u) + ((y >> 4) & 1u);
    const uint64_t permutation = (bank & ~1u) + ((bank & 1u) << 6);
    return (offset & 63u) + (((offset & 448u) +
        (((offset & ~uint64_t{511}) + (permutation << 2)) << 1)) << 2);
}

AdrenoA2xxByteRange AdrenoA2xxTiledRows(uint64_t base, uint32_t pitch, uint32_t bytes,
    uint32_t first_row, uint32_t end_row) {
    constexpr uint64_t kSpill = 4096u;
    if (end_row <= first_row) return {base, base};
    const uint64_t tile_row = uint64_t(pitch) * 32u * bytes;
    const uint64_t begin    = base + (first_row >> 5) * tile_row;
    const uint64_t end      = base + (((end_row - 1u) >> 5) + 1u) * tile_row;
    return {begin > kSpill ? begin - kSpill : 0u, end + kSpill};
}

}
