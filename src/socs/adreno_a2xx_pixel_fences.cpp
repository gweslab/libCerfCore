#include "adreno_a2xx_pixel_fences.h"
#include "adreno_a2xx_memory.h"
#include "adreno_a2xx_socs.h"
#include "adreno_a2xx_texture_layout.h"
#include "adreno_a2xx_tiling.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(AdrenoA2xxPixelFences);

bool AdrenoA2xxPixelFences::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

void AdrenoA2xxPixelFences::OnReady() { memory_ = &emu_.Get<AdrenoA2xxMemory>(); }

void AdrenoA2xxPixelFences::Collect(const AdrenoA2xxPrimitiveJob& job, std::vector<GuestPageFences::Range>& writes,
                                    std::vector<GuestPageFences::Range>& reads) {
    bool shades = false;
    for (const AdrenoA2xxTriangleJob& triangle : job.triangles) {
        CollectTarget(job, triangle, writes, reads);
        shades = shades || (!triangle.resolve && !triangle.depth_only);
    }
    if (shades) CollectFetches(job, reads);
}

void AdrenoA2xxPixelFences::CollectTarget(const AdrenoA2xxPrimitiveJob& job, const AdrenoA2xxTriangleJob& t,
                                          std::vector<GuestPageFences::Range>& writes,
                                          std::vector<GuestPageFences::Range>& reads) {
    if ((t.gmem && !t.resolve) || t.depth_only) return;
    if (t.tiled) {
        const AdrenoA2xxByteRange rows = AdrenoA2xxTiledRows(t.target_base, t.target_pitch, t.bytes,
                                                             uint32_t(t.top), uint32_t(t.bottom));
        memory_->CollectPages(rows.begin, rows.end - rows.begin, true, job.mmu, writes, reads);
        return;
    }
    const uint64_t first = (uint64_t(t.top + t.offset_y) * t.target_pitch + t.left + t.offset_x) * t.bytes;
    const uint64_t end   = (uint64_t(t.bottom - 1 + t.offset_y) * t.target_pitch + t.right + t.offset_x) * t.bytes;
    memory_->CollectPages(uint64_t(t.target_base) + first, end - first, true, job.mmu, writes, reads);
}

void AdrenoA2xxPixelFences::CollectFetches(const AdrenoA2xxPrimitiveJob& job,
                                           std::vector<GuestPageFences::Range>& reads) {
    auto stored = [&](uint32_t index, uint32_t& value) {
        const auto found = job.registers->find(index);
        if (found == job.registers->end()) return false;
        value = found->second;
        return true;
    };
    std::vector<AdrenoA2xxByteRange> extents;
    for (const uint16_t index : job.program->fetches) {
        const AdrenoA2xxAluInstruction& fetch = job.program->alu[index];
        const uint32_t word = fetch.words[0];
        const uint32_t slot = (word >> 20) & 31u;
        if ((word & 31u) == 0u) {
            const uint32_t select = (word >> 25) & 3u;
            uint32_t base = 0, size = 0;
            if (select == 3u || !stored(0x4800u + slot * 6u + select * 2u, base) ||
                !stored(0x4801u + slot * 6u + select * 2u, size)) continue;
            memory_->CollectPages(base & ~3u, size, false, job.mmu, reads, reads);
            continue;
        }
        std::array<uint32_t, 6> state = fetch.texture_state;
        bool known = fetch.texture_resolved;
        for (uint32_t i = 0; !known && i < state.size(); ++i) {
            if (!stored(0x4800u + slot * 6u + i, state[i])) break;
            known = i + 1u == state.size();
        }
        if (!known) continue;
        extents.clear();
        AdrenoA2xxTextureLayout{state}.Extents(extents);
        for (const AdrenoA2xxByteRange& extent : extents)
            memory_->CollectPages(extent.begin, extent.end - extent.begin, false, job.mmu, reads, reads);
    }
}

}
