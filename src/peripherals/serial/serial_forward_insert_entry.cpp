#include "serial_forward_insert_entry.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/ui_text_service.h>
#include "../../platform/host_serial_port.h"

#include <utility>

namespace cerfcore {

namespace {

constexpr const char* kActiveSyncUrl = "https://cerf.cx/activesync";

}

REGISTER_SERVICE(SerialForwardInsertEntry);

SlotEntry SerialForwardInsertEntry::BuildInsertEntry(
    std::function<void(std::string host_port)> on_insert) {
    SlotEntry entry;

    const std::vector<std::string> ports = host_platform::EnumerateSerialPorts();
    if (ports.empty()) {
        entry.details.push_back(WidgetControl{GroupControl{
            "", {WidgetControl{InfoControl{GetUiText(emu_, "pcmcia.no_serial_ports", "   (no host serial ports found)")}}}}});
    } else {
        for (const std::string& p : ports) {
            SlotEntry port;
            port.id     = p;
            port.label  = p;
            port.insert = [on_insert, p] { on_insert(p); };
            entry.variants.push_back(std::move(port));
        }
    }

    entry.details.push_back(WidgetControl{GroupControl{
        "", {WidgetControl{LinkControl{
            GetUiText(emu_, "pcmcia.activesync_help", "Connecting to ActiveSync tutorial"), kActiveSyncUrl}}}}});
    return entry;
}

}
