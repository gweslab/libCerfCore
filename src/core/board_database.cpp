#include <cerfcore/core/board_database.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/cpu_arch_name.h>
#include <cerfcore/core/support_level_text.h>

#include "name_table.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(BoardDatabase);

namespace {

using nlohmann::json;

constexpr char kGaSharedFolderMountPoint[] = "ga_shared_folder_mount_point";
constexpr char kGaVideoDriverNames[]       = "ga_video_driver_names";
constexpr char kSocFamilies[]              = "soc_families";
constexpr char kSocs[]                     = "socs";
constexpr char kDeviceFeatures[]           = "device_features";
constexpr char kBoardFamilies[]            = "board_families";
constexpr char kBoards[]                   = "boards";
constexpr char kId[]                       = "id";
constexpr char kName[]                     = "name";
constexpr char kArch[]                     = "arch";
constexpr char kFamilyId[]                 = "family_id";
constexpr char kIcon[]                     = "icon";
constexpr char kShortName[]                = "short_name";
constexpr char kBoardFamilyId[]            = "board_family_id";
constexpr char kSupportLevel[]             = "support_level";
constexpr char kSocId[]                    = "soc_id";
constexpr char kLcdPanelSize[]             = "lcd_panel_size";
constexpr char kWidth[]                    = "width";
constexpr char kHeight[]                   = "height";
constexpr char kGaColorDepth[]             = "ga_color_depth";
constexpr char kDynamicScaleFactor[]       = "dynamic_scale_factor";
constexpr char kRomPlacingMode[]           = "rom_placing_mode";
constexpr char kConfigurableScreen[]       = "configurable_screen";
constexpr char kDeviceYear[]               = "device_year";
constexpr char kNotes[]                    = "notes";
constexpr char kFeatureId[]                = "feature_id";
constexpr char kSupported[]                = "supported";

constexpr std::pair<std::string_view, SupportLevel> kSupportLevels[] = {
    {CERF_SUPPORT_BOOTABLE_FULL, SupportLevel::BootableFull},
    {CERF_SUPPORT_BOOTABLE_PARTIAL, SupportLevel::BootablePartial},
    {CERF_SUPPORT_UNKNOWN_PARTIAL, SupportLevel::UnknownPartial},
    {CERF_SUPPORT_NO_SUPPORT, SupportLevel::NoSupport},
};

constexpr std::pair<std::string_view, RomPlacingMode> kRomPlacingModes[] = {
    {"flat_container", RomPlacingMode::FlatContainer},
    {"imx51_nand", RomPlacingMode::Imx51Nand},
};

bool InWordList(std::string_view list, std::string_view word) {
    while (!list.empty()) {
        const size_t end = list.find(' ');
        if (list.substr(0, end) == word) return true;
        if (end == std::string_view::npos) return false;
        list.remove_prefix(end + 1);
    }
    return false;
}

template <class Row>
const Row& RowById(const std::vector<Row>& rows, std::string_view id) {
    return *std::find_if(rows.begin(), rows.end(), [id](const Row& row) { return row.id == id; });
}

struct BoardValues {
    const json& board;
    const json& family;

    const json& Owner(const char* key) const { return board.contains(key) ? board : family; }

    template <class T>
    T Value(const char* key) const {
        return Owner(key).at(key).template get<T>();
    }

    template <class T>
    T ValueOr(const char* key, const T& absent) const {
        return Owner(key).value(key, absent);
    }
};

void MergeFeatures(const json& row, std::vector<DbFeatureSupport>& out) {
    const json list = row.value(kDeviceFeatures, json::array());
    for (const auto& f : list) {
        std::string id        = f.at(kFeatureId).get<std::string>();
        const bool  supported = f.at(kSupported).get<bool>();
        const auto  existing  = std::find_if(out.begin(), out.end(),
                                             [&id](const DbFeatureSupport& s) { return s.feature_id == id; });
        if (existing != out.end()) existing->supported = supported;
        else                       out.push_back(DbFeatureSupport{std::move(id), supported});
    }
}

}

void BoardDatabase::OnReady() {
    const json root = CfgReadInstalledJsonFile(DbJsonPath());

    ga_shared_folder_mount_point_ = root.at(kGaSharedFolderMountPoint).get<std::string>();
    ga_video_driver_names_        = root.at(kGaVideoDriverNames).get<std::vector<std::string>>();

    for (const auto& f : root.at(kSocFamilies))
        soc_families_.push_back(DbSocFamily{f.at(kId).get<std::string>(), f.at(kName).get<std::string>(),
                                            CpuArchFromName(f.at(kArch).get<std::string>())});

    for (const auto& s : root.at(kSocs))
        socs_.push_back(DbSoc{s.at(kId).get<std::string>(), s.at(kName).get<std::string>(),
                              s.at(kFamilyId).get<std::string>()});

    for (const auto& f : root.at(kDeviceFeatures)) {
        DbDeviceFeature row;
        row.id   = f.at(kId).get<std::string>();
        row.name = f.at(kName).get<std::string>();
        row.icon = f.value(kIcon, row.id);
        device_features_.push_back(std::move(row));
    }

    const json& families = root.at(kBoardFamilies);
    for (const auto& f : families)
        board_families_.push_back(DbBoardFamily{f.at(kId).get<std::string>(), f.at(kName).get<std::string>()});

    for (const auto& b : root.at(kBoards)) {
        DbBoard row;
        row.id         = b.at(kId).get<std::string>();
        row.name       = b.at(kName).get<std::string>();
        row.short_name = b.value(kShortName, row.name);
        row.family_id  = b.at(kBoardFamilyId).get<std::string>();
        const json& family = *std::find_if(families.begin(), families.end(),
                                           [&row](const json& f) { return f.at(kId) == row.family_id; });
        const BoardValues v{b, family};
        const std::string support = b.at(kSupportLevel).get<std::string>();
        row.support_level = ValueByName(kSupportLevels, support);
        row.runs          = InWordList(CERF_RUNNING_SUPPORT_LEVELS, support);
        row.soc_id        = v.Value<std::string>(kSocId);
        const json& panel_owner = v.Owner(kLcdPanelSize);
        if (panel_owner.contains(kLcdPanelSize)) {
            const json& p = panel_owner.at(kLcdPanelSize);
            row.lcd_panel_size = DbLcdPanelSize{p.at(kWidth).get<uint32_t>(), p.at(kHeight).get<uint32_t>()};
        }
        row.ga_color_depth      = v.ValueOr(kGaColorDepth, kDefaultGaColorDepth);
        row.dynamic_scale_factor = v.ValueOr(kDynamicScaleFactor, 0);
        row.rom_placing_mode    = ValueByName(kRomPlacingModes, v.Value<std::string>(kRomPlacingMode));
        row.configurable_screen = v.ValueOr(kConfigurableScreen, false);
        row.device_year         = v.ValueOr(kDeviceYear, 0u);
        row.notes = family.value(kNotes, std::vector<std::string>());
        for (std::string& note : b.value(kNotes, std::vector<std::string>())) row.notes.push_back(std::move(note));
        MergeFeatures(family, row.features);
        MergeFeatures(b, row.features);
        boards_.push_back(std::move(row));
    }
}

const DbBoard* BoardDatabase::FindBoard(std::string_view id) const {
    for (const auto& b : boards_)
        if (b.id == id) return &b;
    return nullptr;
}

const DbSoc& BoardDatabase::Soc(std::string_view id) const { return RowById(socs_, id); }

const DbBoardFamily& BoardDatabase::BoardFamily(std::string_view id) const { return RowById(board_families_, id); }

const DbSocFamily& BoardDatabase::SocFamily(std::string_view id) const { return RowById(soc_families_, id); }

CpuArch BoardDatabase::SocArch(std::string_view soc_id) const { return SocFamily(Soc(soc_id).family_id).arch; }

}
