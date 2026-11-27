#include "kernel_image_copier.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../boards/page_table_builder.h"

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(KernelImageCopier);

bool KernelImageCopier::IsVolatilePa(uint32_t pa) {
    for (const auto& r : emu_.Get<PageTableBuilder>().BackedMemoryRegions()) {
        if (pa < r.pa_base || pa >= r.pa_base + r.size) continue;
        return r.kind != MemoryKind::kFlash && r.kind != MemoryKind::kRom;
    }
    return true;
}

void KernelImageCopier::CopyToKernelVa(uint32_t va, const uint8_t* src, uint32_t length,
                                       bool volatile_only, const char* image) {
    auto& page_tables = emu_.Get<PageTableBuilder>();
    auto& mem         = emu_.Get<EmulatedMemory>();
    const uint32_t end = va + length;
    uint32_t run_va = va;
    while (run_va < end) {
        const uint32_t run_pa = page_tables.VaToPa(run_va);
        uint32_t next = run_va;
        do {
            next += 0x1000u;
        } while (next < end && page_tables.VaToPa(next) == run_pa + (next - run_va));
        const uint32_t run_end = std::min(next, end);
        const uint32_t run_len = run_end - run_va;
        if (!volatile_only || IsVolatilePa(run_pa)) {
            mem.CopyIn(run_pa, src + (run_va - va), run_len);
            LOG(Boot, "%s: run %u bytes  kva=0x%08X..0x%08X  pa=0x%08X\n",
                image, run_len, run_va, run_end, run_pa);
        }
        run_va = run_end;
    }
}

}
