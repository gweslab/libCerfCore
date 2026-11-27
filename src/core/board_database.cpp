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

constexpr char kGuestAdditions[]           = "guest_additions";
constexpr char kGaSharedFolderMountPoint[] = "ga_shared_folder_mount_point";
constexpr char kGaVideoDriverNames[]       = "ga_video_driver_names";
constexpr char kValues[]                   = "values";
constexpr char kSocFamilies[]              = "soc_families";
constexpr char kSocs[]                     = "socs";
constexpr char kDeviceGenericParts[]       = "device_generic_parts";
constexpr char kDeviceSpecs[]              = "device_specs";
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
constexpr char kGaColorDepth[]             = "ga_color_depth";
constexpr char kGaWindowSize[]             = "ga_window_size";
constexpr char kDynamicScaleFactor[]       = "dynamic_scale_factor";
constexpr char kRomPlacingMode[]           = "rom_placing_mode";
constexpr char kConfigurableScreen[]       = "configurable_screen";
constexpr char kDeviceYear[]               = "device_year";
constexpr char kLedColor[]                 = "led_color";
constexpr char kHwRevision[]               = "hw_revision";
constexpr char kNotes[]                    = "notes";
constexpr char kGenericPartId[]            = "generic_part_id";
constexpr char kSupported[]                = "supported";
constexpr char kParts[]                    = "parts";
constexpr char kUnknownName[]              = "unknown_name";
constexpr char kConnections[]              = "connections";
constexpr char kBoardIds[]                 = "board_ids";
constexpr char kBoardFamilyIds[]           = "board_family_ids";
constexpr char kWidth[]                    = "width";
constexpr char kHeight[]                   = "height";
constexpr char kSizeMb[]                   = "size_mb";
constexpr char kChipCount[]                = "chip_count";
constexpr char kClampViewport[]            = "clamp_viewport";
constexpr char kRowBits[]                  = "row_bits";
constexpr char kColumnBits[]               = "column_bits";
constexpr char kBankBits[]                 = "bank_bits";
constexpr char kTechnology[]               = "technology";
constexpr char kChemistry[]                = "chemistry";
constexpr char kBoardConfigParams[]        = "board_config_params";
constexpr char kBoardConfig[]              = "board_config";
constexpr char kDefault[]                  = "default";
constexpr char kEnumValues[]               = "enum_values";
constexpr char kPartPicker[]               = "part_picker";
constexpr char kPartId[]                   = "part_id";

constexpr std::pair<std::string_view, SupportLevel> kSupportLevels[] = {
    {CERF_SUPPORT_BOOTABLE_FULL, SupportLevel::BootableFull},
    {CERF_SUPPORT_BOOTABLE_PARTIAL, SupportLevel::BootablePartial},
    {CERF_SUPPORT_UNKNOWN_PARTIAL, SupportLevel::UnknownPartial},
    {CERF_SUPPORT_NO_SUPPORT, SupportLevel::NoSupport},
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

std::optional<uint32_t> OptionalNumber(const json& item, const char* key) {
    if (!item.contains(key)) return std::nullopt;
    return item.at(key).get<uint32_t>();
}

void AppendFeatures(const json& row, std::vector<DbFeature>& out) {
    const json list = row.value(kDeviceSpecs, json::array());
    for (const auto& s : list)
        out.push_back(DbFeature{s.at(kGenericPartId).get<std::string>(), s.at(kSupported).get<bool>()});
}

DbPart ParsePart(const json& p) {
    DbPart part;
    part.id              = p.at(kId).get<std::string>();
    part.name            = p.value(kName, std::string());
    part.generic_part_id = p.at(kGenericPartId).get<std::string>();
    part.unknown_name    = p.value(kUnknownName, false);
    part.supported       = p.at(kSupported).get<bool>();
    part.width           = OptionalNumber(p, kWidth);
    part.height          = OptionalNumber(p, kHeight);
    part.size_mb         = OptionalNumber(p, kSizeMb);
    part.chip_count      = OptionalNumber(p, kChipCount);
    part.row_bits        = OptionalNumber(p, kRowBits);
    part.column_bits     = OptionalNumber(p, kColumnBits);
    part.bank_bits       = OptionalNumber(p, kBankBits);
    part.technology      = p.value(kTechnology, std::string());
    part.clamp_viewport  = p.value(kClampViewport, false);
    part.chemistry       = p.value(kChemistry, std::string());
    return part;
}

bool Contains(const json& row, const char* key, const std::string& id) {
    for (const auto& v : row.value(key, json::array()))
        if (v.get<std::string>() == id) return true;
    return false;
}

bool ConnectsTo(const json& part, const DbBoard& board) {
    for (const auto& c : part.value(kConnections, json::array()))
        if (Contains(c, kBoardIds, board.id) || Contains(c, kBoardFamilyIds, board.family_id)) return true;
    return false;
}

std::vector<ConfigEnumValue> ParseParamValues(const json& list) {
    std::vector<ConfigEnumValue> out;
    for (const auto& v : list) {
        if (v.contains(kPartId)) out.push_back(ConfigEnumValue{v.at(kPartId).get<std::string>(), std::string()});
        else                     out.push_back(ConfigEnumValue{v.at(kId).get<std::string>(), v.at(kName).get<std::string>()});
    }
    return out;
}

std::optional<DbLcdPanelSize> PanelSize(std::span<const DbPart* const> parts) {
    for (const DbPart* p : parts)
        if (p->width && p->height) return DbLcdPanelSize{*p->width, *p->height};
    return std::nullopt;
}

void MergeBoardConfig(const json& row, std::vector<DbBoardConfigSetting>& out) {
    const json settings = row.value(kBoardConfig, json::object());
    for (const auto& [id, s] : settings.items()) {
        const DbBoardConfigSetting* existing = FindBoardConfigSetting(out, id);
        DbBoardConfigSetting        setting  = existing ? *existing : DbBoardConfigSetting{id, {}, {}};
        if (s.contains(kDefault)) setting.default_value = s.at(kDefault).get<std::string>();
        if (s.contains(kValues)) setting.values = s.at(kValues).get<std::vector<std::string>>();
        if (existing) out[size_t(existing - out.data())] = std::move(setting);
        else          out.push_back(std::move(setting));
    }
}

}

bool DbBoardConfigSetting::Accepts(std::string_view value) const {
    return std::find(values.begin(), values.end(), value) != values.end();
}

const DbBoardConfigSetting* FindBoardConfigSetting(std::span<const DbBoardConfigSetting> settings,
                                                   std::string_view param_id) {
    for (const DbBoardConfigSetting& s : settings)
        if (s.param_id == param_id) return &s;
    return nullptr;
}

void BoardDatabase::OnReady() {
    const json root = CfgReadInstalledJsonFile(DbJsonPath());

    const json& guest_additions   = root.at(kGuestAdditions);
    ga_shared_folder_mount_point_ = guest_additions.at(kGaSharedFolderMountPoint).get<std::string>();
    ga_video_driver_names_        = guest_additions.at(kGaVideoDriverNames).get<std::vector<std::string>>();

    for (const auto& f : root.at(kSocFamilies))
        soc_families_.push_back(DbSocFamily{f.at(kId).get<std::string>(), f.at(kName).get<std::string>(),
                                            CpuArchFromName(f.at(kArch).get<std::string>())});

    for (const auto& s : root.at(kSocs))
        socs_.push_back(DbSoc{s.at(kId).get<std::string>(), s.at(kName).get<std::string>(),
                              s.at(kFamilyId).get<std::string>()});

    for (const auto& p : root.at(kDeviceGenericParts))
        generic_parts_.push_back(DbGenericPart{p.at(kId).get<std::string>(), p.at(kName).get<std::string>(),
                                               p.at(kIcon).get<std::string>()});

    const json& parts = root.at(kParts);
    for (const auto& p : parts) parts_.push_back(ParsePart(p));

    for (const auto& p : root.at(kBoardConfigParams)) {
        const bool        picker  = p.value(kPartPicker, false);
        const std::string generic = p.value(kGenericPartId, std::string());
        board_config_params_.push_back(DbBoardConfigParam{
            p.at(kId).get<std::string>(), picker ? GenericPart(generic).name : p.at(kName).get<std::string>(), generic,
            picker, ParseParamValues(p.at(kEnumValues))});
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
        AppendFeatures(family, row.features);
        AppendFeatures(b, row.features);
        row.ga_color_depth      = v.ValueOr(kGaColorDepth, kDefaultGaColorDepth);
        row.ga_window_size      = v.ValueOr(kGaWindowSize, kDefaultGaWindowSize);
        row.dynamic_scale_factor = v.ValueOr(kDynamicScaleFactor, 0);
        row.rom_placing_mode    = v.Value<std::string>(kRomPlacingMode);
        row.configurable_screen = v.ValueOr(kConfigurableScreen, false);
        row.device_year         = v.ValueOr(kDeviceYear, 0u);
        if (v.Owner(kLedColor).contains(kLedColor)) row.led_color = v.Value<std::array<uint8_t, 3>>(kLedColor);
        if (v.Owner(kHwRevision).contains(kHwRevision)) row.hw_revision = v.Value<uint32_t>(kHwRevision);
        row.notes = family.value(kNotes, std::vector<std::string>());
        for (std::string& note : b.value(kNotes, std::vector<std::string>())) row.notes.push_back(std::move(note));
        MergeBoardConfig(family, row.config);
        MergeBoardConfig(b, row.config);
        std::ranges::sort(row.config, {}, [this](const DbBoardConfigSetting& s) { return FindBoardConfigParam(s.param_id); });
        for (size_t i = 0; i < parts.size(); ++i)
            if (ConnectsTo(parts[i], row)) row.connected_parts.push_back(&parts_[i]);
        row.parts = ComposeParts(row, [this](const DbBoardConfigSetting& s) { return FindPart(s.default_value); });
        std::ranges::sort(row.parts, {}, [this](const DbPart* p) {
            return std::pair(&GenericPart(p->generic_part_id), p);
        });
        row.lcd_panel_size = PanelSize(row.parts);
        boards_.push_back(std::move(row));
    }
}

const DbGenericPart& BoardDatabase::GenericPart(std::string_view id) const { return RowById(generic_parts_, id); }

const DbPart* BoardDatabase::FindPart(std::string_view id) const {
    for (const DbPart& p : parts_)
        if (p.id == id) return &p;
    return nullptr;
}

const DbBoardConfigParam* BoardDatabase::FindBoardConfigParam(std::string_view id) const {
    for (const auto& p : board_config_params_)
        if (p.id == id) return &p;
    return nullptr;
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
