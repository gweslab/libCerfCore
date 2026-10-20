#include "hp_palmtop_vga_insert_entry.h"

#include "hp_palmtop_vga_card.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/ui_text_service.h>

#include <memory>

namespace cerfcore {

REGISTER_SERVICE(HpPalmtopVgaInsertEntry);

SlotEntry HpPalmtopVgaInsertEntry::BuildInsertEntry(PcmciaCardCatalog::CardInserter inserter) {
    SlotEntry entry;
    entry.insert = [this, inserter] {
        inserter(std::make_unique<HpPalmtopVgaCard>(emu_));
    };
    entry.details = {
        WidgetControl{InfoControl{GetUiText(emu_, "pcmcia.hp_vga.external_output", "External VGA monitor output, in its own window.")}},
        WidgetControl{InfoControl{GetUiText(emu_, "pcmcia.hp_vga.jornada_support", "Works on the HP Jornada series.")}},
        WidgetControl{InfoControl{GetUiText(emu_, "pcmcia.hp_vga.verified_720", "Verified on the HP Jornada 720.")}},
    };
    return entry;
}

}
