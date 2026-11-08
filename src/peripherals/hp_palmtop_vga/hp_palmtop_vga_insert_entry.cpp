#include "hp_palmtop_vga_insert_entry.h"

#include "hp_palmtop_vga_card.h"
#include <cerfcore/core/cerf_emulator.h>

#include <memory>

namespace cerfcore {

REGISTER_SERVICE(HpPalmtopVgaInsertEntry);

SlotEntry HpPalmtopVgaInsertEntry::BuildInsertEntry(PcmciaCardCatalog::CardInserter inserter) {
    SlotEntry entry;
    entry.insert = [this, inserter] {
        inserter(std::make_unique<HpPalmtopVgaCard>(emu_));
    };
    entry.details = {
        WidgetControl{InfoControl{"Only HP Jornada devices include the driver"}},
    };
    return entry;
}

}
