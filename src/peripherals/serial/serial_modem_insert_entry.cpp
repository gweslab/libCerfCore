#include "serial_modem_insert_entry.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/ui_text_service.h>

#include <utility>

namespace cerfcore {

namespace {

constexpr const char* kGettingOnlineUrl = "https://cerf.cx/getting-online";

}

REGISTER_SERVICE(SerialModemInsertEntry);

SlotEntry SerialModemInsertEntry::BuildInsertEntry(std::function<void()> on_insert) {
    SlotEntry entry;
    entry.insert  = std::move(on_insert);
    entry.details = {WidgetControl{LinkControl{
        GetUiText(emu_, "pcmcia.getting_online_help", "Getting online tutorial"), kGettingOnlineUrl}}};
    return entry;
}

}
