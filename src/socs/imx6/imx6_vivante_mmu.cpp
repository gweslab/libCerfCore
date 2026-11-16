#include "imx6_vivante_mmu.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>

namespace cerfcore {

namespace imx6_vivante {

uint32_t VivanteMmu::PageTableRegister(MmuClient client) {
    static constexpr uint32_t registers[] = {
        kMmuv1FePageTable, kMmuv1TxPageTable,
        kMmuv1PePageTable, kMmuv1RaPageTable,
    };
    return registers[static_cast<uint32_t>(client)];
}

uint32_t VivanteMmu::MemoryBaseRegister(MmuClient client) {
    static constexpr uint32_t registers[] = {
        kMmuv1MemoryBaseFe, kMmuv1MemoryBaseTx,
        kMmuv1MemoryBasePe, kMmuv1MemoryBaseRa,
    };
    return registers[static_cast<uint32_t>(client)];
}
const uint8_t* VivanteMmu::PageTableEntryToHost(uint32_t gpu_address, MmuClient client) const {
    if (gpu_address < kMmuv1GpuMemStart) return nullptr;
    const uint32_t page = (gpu_address - kMmuv1GpuMemStart) >> 12;
    const uint32_t table = state_.regs_[PageTableRegister(client) >> 2];
    return emu_.Get<EmulatedMemory>().TryTranslateRange(table + page * 4u, sizeof(uint32_t));
}

bool VivanteMmu::TranslateMmuv1(uint32_t gpu_address, MmuClient client,
                                uint32_t& physical) const {
    if (gpu_address < kMmuv1GpuMemStart) {
        physical = state_.regs_[MemoryBaseRegister(client) >> 2] + gpu_address;
        return true;
    }

    const uint8_t* entry = PageTableEntryToHost(gpu_address, client);
    if (!entry) return false;
    const uint32_t pte = cerf::le::U32(entry);
    physical = (pte & kMmuv1PageMask) | (gpu_address & ~kMmuv1PageMask);
    return true;
}
const uint8_t* VivanteMmu::TranslateToHost(uint32_t gpu_address, size_t size,
                                           MmuClient client) const {
    uint32_t physical = 0u;
    if (!TranslateMmuv1(gpu_address, client, physical)) return nullptr;
    return emu_.Get<EmulatedMemory>().TryTranslateRange(physical, size);
}

uint8_t* VivanteMmu::TranslateToHostWrite(uint32_t gpu_address, size_t size,
                                          MmuClient client) const {
    uint32_t physical = 0u;
    if (!TranslateMmuv1(gpu_address, client, physical)) return nullptr;
    return emu_.Get<EmulatedMemory>().TryTranslateRange(physical, size, true);
}

}

}
