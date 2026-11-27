#include "boot_mode.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../core/no_emulation_runtime_service.h"
#include "../boards/page_table_builder.h"

namespace cerfcore {

namespace {

constexpr uint32_t kThumbEntryBit = 1u;

}

void BootMode::OnReady() {
    emu_.Get<NoEmulationRuntimeService>().EnsureEmulationPrevented();
}

uint32_t BootMode::KernelEntryPa(uint32_t entry_va) {
    return emu_.Get<PageTableBuilder>().VaToPa(entry_va & ~kThumbEntryBit);
}

bool BootMode::KernelEntryThumb(uint32_t entry_va) { return (entry_va & kThumbEntryBit) != 0u; }

}
