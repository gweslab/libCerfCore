#include "page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <typeinfo>

namespace cerfcore {

uint32_t PageTableBuilder::InitStackTopPa() const {
    emu_.Get<Fatal>().Die("PageTableBuilder::InitStackTopPa: this board declares no "
                          "bootloader-handoff SP");
}

DramRegion PageTableBuilder::EntryXipRomRegion() const {
    emu_.Get<Fatal>().Die("%s declares no entry-XIP ROM region", typeid(*this).name());
}

}
