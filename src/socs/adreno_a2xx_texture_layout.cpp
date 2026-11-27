#include "adreno_a2xx_texture_layout.h"

namespace cerfcore {

void AdrenoA2xxTextureLayout::Extents(std::vector<AdrenoA2xxByteRange>& out) const {
    if ((state_[0] & 3u) != 2u || ((state_[1] | state_[5]) & 0xFFFFF000u) == 0u || bytes_ == 0u) return;
    const bool     tiled  = (state_[0] & 0x80000000u) != 0u;
    const uint32_t levels = ((state_[4] >> 6) & 15u) != 0u ? last_level_ + 1u : 1u;
    const uint32_t faces  = cube_ ? 6u : 1u;
    for (uint32_t face = 0; face < faces; ++face)
        for (uint32_t level = 0; level < levels; ++level) {
            const AdrenoA2xxTextureLevel lod = Level(level, face);
            if (lod.base > UINT32_MAX) continue;
            if (tiled) out.push_back(AdrenoA2xxTiledRows(lod.base, lod.pitch, bytes_, lod.mip_y, lod.mip_y + lod.height));
            else       out.push_back({lod.base, lod.base + LinearBytes()});
        }
}

}
