#include "pcmcia_card_catalog.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../compactflash/compactflash_card.h"
#include "../compactflash/compactflash_commands.h"
#include "../hp_f1252a/hp_f1252a_card.h"
#include "../realtek_rtl8019/rtl8019.h"
#include "../serial/serial_forward_commands.h"
#include "../serial/serial_tcp_address.h"
#include "../serial/serial_tcp_commands.h"
#include "../serial_pccard/serial_pccard.h"

#include <utility>

namespace cerfcore {

namespace {

const char kIdCf[]        = "cf";
const char kIdHpVga[]     = "hpvga";
const char kIdSerial[]    = "serial";
const char kIdSerialFwd[] = "serial_fwd";
const char kInsert[]      = "insert";

}

void PcmciaCardCatalog::OnReady() {
    const CommandList insert = [] { return std::vector<InsertCommand>{InsertCommand{kInsert, {}}}; };
    AddInsertOnly(Rtl8019::kSaveId, Rtl8019::kDisplayName, insert,
                  [this](const std::string&) { return std::make_unique<Rtl8019>(emu_, Rtl8019::Supply::k5V); });
    AddInsertOnly(Rtl8019::kSaveId3V3, Rtl8019::kDisplayName3V3, insert,
                  [this](const std::string&) { return std::make_unique<Rtl8019>(emu_, Rtl8019::Supply::k3V3); });
    entries_.push_back(Entry{
        kIdCf, CompactFlashCard::kDisplayName, [this] { return emu_.Get<CompactFlashCommands>().Commands(); },
        [this](MachineObject& socket, const std::string& command_id, const std::string& argument,
               CardInserter inserter) {
            emu_.Get<CompactFlashCommands>().Run(socket, command_id, argument, std::move(inserter));
        },
        [this](const std::string& binding) { return std::make_unique<CompactFlashCard>(emu_, binding); }});
    AddInsertOnly(kIdHpVga, HpF1252aCard::kDisplayName, insert,
                  [this](const std::string&) { return std::make_unique<HpF1252aCard>(emu_); });
    AddInsertOnly(kIdSerial, SerialPcCard::kDisplayName, insert,
                  [this](const std::string&) { return std::make_unique<SerialPcCard>(emu_); });
    AddInsertOnly(kIdSerialFwd, SerialPcCard::kForwardDisplayName,
                  [this] { return emu_.Get<SerialForwardCommands>().Commands(); },
                  [this](const std::string& port) { return std::make_unique<SerialPcCard>(emu_, port); });
    entries_.push_back(Entry{
        SerialPcCard::kTcpSaveId, SerialPcCard::kTcpDisplayName,
        [this] { return emu_.Get<SerialTcpCommands>().Commands(); },
        [this](MachineObject& socket, const std::string&, const std::string&, CardInserter inserter) {
            emu_.Get<SerialTcpCommands>().Run(socket, [this, inserter](const SerialTcpAddress& address) {
                inserter.insert(std::make_unique<SerialPcCard>(emu_, address));
            });
        },
        [this](const std::string& binding) {
            return std::make_unique<SerialPcCard>(emu_, *SerialTcpAddress::Parse(binding));
        }});
}

void PcmciaCardCatalog::AddInsertOnly(std::string id, std::string display_name, CommandList commands,
                                      CardFactory create) {
    CommandRun run = [create](MachineObject&, const std::string&, const std::string& argument,
                              CardInserter inserter) { inserter.insert(create(argument)); };
    entries_.push_back(
        Entry{std::move(id), std::move(display_name), std::move(commands), std::move(run), std::move(create)});
}

const PcmciaCardCatalog::Entry* PcmciaCardCatalog::Find(const std::string& id) const {
    for (const Entry& entry : entries_)
        if (entry.id == id) return &entry;
    return nullptr;
}

std::vector<InsertableType> PcmciaCardCatalog::Accepted() const {
    std::vector<InsertableType> types;
    types.reserve(entries_.size());
    for (const Entry& entry : entries_) types.push_back(InsertableType{entry.id, entry.display_name, entry.commands()});
    return types;
}

void PcmciaCardCatalog::Insert(MachineObject& socket, const std::string& type_id, const std::string& command_id,
                               const std::string& argument, CardInserter inserter) {
    Find(type_id)->run(socket, command_id, argument, std::move(inserter));
}

std::unique_ptr<PcmciaCard> PcmciaCardCatalog::TryCreate(const std::string& id, const std::string& binding) {
    const Entry* entry = Find(id);
    if (entry == nullptr) return nullptr;
    return entry->create(binding);
}

std::unique_ptr<PcmciaCard> PcmciaCardCatalog::Create(const std::string& id, const std::string& binding) {
    auto card = TryCreate(id, binding);
    if (!card) emu_.Get<Fatal>().Die("PcmciaCardCatalog::Create: unknown card id '%s'", id.c_str());
    return card;
}

REGISTER_SERVICE(PcmciaCardCatalog);

}
