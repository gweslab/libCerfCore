#include <cerfcore/core/device_file_types.h>

#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(DeviceFileTypes);

namespace {

using nlohmann::json;
using Strings = std::vector<std::string>;

constexpr char kRomTypes[]     = "rom_types";
constexpr char kStorageTypes[] = "storage_types";

std::vector<DeviceFileType> ReadTable(const json& root, const char* table, DeviceFileSection section) {
    std::vector<DeviceFileType> out;
    for (const auto& r : root.at(table)) {
        DeviceFileType t;
        t.section                  = section;
        t.id                       = r.at("id").get<std::string>();
        t.name                     = r.at("name").get<std::string>();
        t.formats                  = r.value("formats", Strings());
        t.note                     = r.value("note", std::string());
        t.optional                 = r.value("optional", false);
        t.optional_for_storage_ids = r.value("optional_for_storage_ids", Strings());
        t.board_ids                = r.value("board_ids", Strings());
        t.board_family_ids         = r.value("board_family_ids", Strings());
        out.push_back(std::move(t));
    }
    return out;
}

bool Contains(const std::vector<std::string>& list, std::string_view value) {
    return !value.empty() && std::find(list.begin(), list.end(), value) != list.end();
}

void PickForBoard(const std::vector<DeviceFileType>& rows, std::string_view board_id,
                  std::string_view family_id, std::vector<DeviceFileType>& out) {
    std::vector<std::string> ids;
    for (const auto& r : rows)
        if (std::find(ids.begin(), ids.end(), r.id) == ids.end()) ids.push_back(r.id);
    for (const auto& id : ids) {
        const DeviceFileType* bound   = nullptr;
        const DeviceFileType* unbound = nullptr;
        for (const auto& r : rows) {
            if (r.id != id) continue;
            if (!bound && (Contains(r.board_ids, board_id) || Contains(r.board_family_ids, family_id)))
                bound = &r;
            if (!unbound && r.board_ids.empty() && r.board_family_ids.empty()) unbound = &r;
        }
        if (bound)        out.push_back(*bound);
        else if (unbound) out.push_back(*unbound);
    }
}

}

void DeviceFileTypes::OnReady() {
    const json root = CfgReadInstalledJsonFile(DbJsonPath());
    rom_types_     = ReadTable(root, kRomTypes, DeviceFileSection::kRom);
    storage_types_ = ReadTable(root, kStorageTypes, DeviceFileSection::kStorage);
}

std::vector<DeviceFileType> DeviceFileTypes::ForBoard(std::string_view board_id) const {
    const DbBoard*   board     = emu_.Get<BoardDatabase>().FindBoard(board_id);
    std::string_view family_id = board ? std::string_view(board->family_id) : std::string_view();
    std::vector<DeviceFileType> out;
    PickForBoard(rom_types_, board_id, family_id, out);
    PickForBoard(storage_types_, board_id, family_id, out);
    return out;
}

}
