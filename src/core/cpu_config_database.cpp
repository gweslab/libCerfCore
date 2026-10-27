#include <cerfcore/core/cpu_config_database.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/cpu_arch_name.h>

#include "name_table.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace cerfcore {

REGISTER_SERVICE(CpuConfigDatabase);

namespace {

using nlohmann::json;
namespace db_key = cpu_config_db_key;

constexpr char kId[]         = "id";
constexpr char kName[]       = "name";
constexpr char kArch[]       = "arch";
constexpr char kEnumValues[] = "enum_values";
constexpr char kSocId[]      = "soc_id";
constexpr char kInherits[]   = "inherits_cpu_config_id";
constexpr char kValues[]     = "values";

constexpr std::pair<std::string_view, CpuConfigParamType> kParamTypes[] = {
    {"boolean", CpuConfigParamType::Boolean},
    {"integer", CpuConfigParamType::Integer},
    {"hex", CpuConfigParamType::Hex},
    {"enum", CpuConfigParamType::Enum},
};

CpuConfigValue ValueOf(const CpuConfigParam& param, const json& value) {
    CpuConfigValue out;
    switch (param.type) {
    case CpuConfigParamType::Boolean: out.boolean = value.get<bool>(); break;
    case CpuConfigParamType::Integer: out.number = value.get<uint32_t>(); break;
    case CpuConfigParamType::Hex:     ParseCpuConfigHex(value.get<std::string>(), out.number); break;
    case CpuConfigParamType::Enum:    out.enum_id = value.get<std::string>(); break;
    }
    return out;
}

}

bool ParseCpuConfigHex(std::string_view text, uint32_t& out) {
    if (text.size() < 3 || text.size() > 10 || text[0] != '0' ||
        (text[1] != 'x' && text[1] != 'X'))
        return false;
    uint32_t value = 0;
    for (size_t i = 2; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!std::isxdigit(c)) return false;
        const uint32_t digit = std::isdigit(c) ? c - '0' : (std::tolower(c) - 'a' + 10);
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

void CpuConfigDatabase::OnReady() {
    const json root = CfgReadInstalledJsonFile(DbJsonPath());

    for (const auto& p : root.at(db_key::kParams)) {
        CpuConfigParam param;
        param.id   = p.at(kId).get<std::string>();
        param.name = p.at(kName).get<std::string>();
        param.arch = CpuArchFromName(p.at(kArch).get<std::string>());
        param.type = ValueByName(kParamTypes, p.at(db_key::kType).get<std::string>());
        if (param.type == CpuConfigParamType::Enum)
            for (const auto& v : p.at(kEnumValues))
                param.enum_values.push_back(
                    CpuConfigEnumValue{v.at(kId).get<std::string>(), v.at(kName).get<std::string>()});
        params_.push_back(std::move(param));
    }

    for (const auto& c : root.at(db_key::kConfigs)) {
        CpuConfigPreset preset;
        preset.id       = c.value(kId, std::string());
        preset.soc_id   = c.value(kSocId, std::string());
        preset.inherits = c.value(kInherits, std::string());
        for (const auto& [key, value] : c.at(kValues).items()) preset.values[key] = ValueOf(*FindParam(key), value);
        presets_.push_back(std::move(preset));
    }
}

const CpuConfigParam* CpuConfigDatabase::FindParam(std::string_view id) const {
    for (const auto& p : params_)
        if (p.id == id) return &p;
    return nullptr;
}

const CpuConfigPreset& CpuConfigDatabase::SocPreset(std::string_view soc_id) const {
    return *std::find_if(presets_.begin(), presets_.end(),
                         [soc_id](const CpuConfigPreset& preset) { return preset.soc_id == soc_id; });
}

CpuConfigValues CpuConfigDatabase::ResolvePreset(std::string_view soc_id) const { return Resolve(SocPreset(soc_id)); }

CpuConfigValues CpuConfigDatabase::Resolve(const CpuConfigPreset& preset) const {
    CpuConfigValues out;
    Apply(preset, out);
    return out;
}

void CpuConfigDatabase::Apply(const CpuConfigPreset& preset, CpuConfigValues& out) const {
    if (!preset.inherits.empty()) Apply(PresetById(preset.inherits), out);
    for (const auto& [key, value] : preset.values) out[key] = value;
}

const CpuConfigPreset& CpuConfigDatabase::PresetById(std::string_view id) const {
    return *std::find_if(presets_.begin(), presets_.end(),
                         [id](const CpuConfigPreset& preset) { return preset.id == id; });
}

CpuConfigValue CpuConfigDatabase::ParseValue(const CpuConfigParam& param, const json& value,
                                             const std::string& path,
                                             const std::string& ctx) const {
    const std::string where = CfgKeyPath({ctx, param.id});
    uint32_t          hex   = 0;
    switch (param.type) {
    case CpuConfigParamType::Boolean:
        if (!value.is_boolean()) CfgFatal(path, "'" + where + "' must be a boolean");
        break;
    case CpuConfigParamType::Integer:
        if (!value.is_number_unsigned() || value.get<uint64_t>() > UINT32_MAX)
            CfgFatal(path, "'" + where + "' must be an unsigned 32-bit integer");
        break;
    case CpuConfigParamType::Hex:
        if (!value.is_string() || !ParseCpuConfigHex(value.get<std::string>(), hex))
            CfgFatal(path, "'" + where + "' must be a 32-bit hex string such as \"0x1F\"");
        break;
    case CpuConfigParamType::Enum:
        if (!value.is_string()) CfgFatal(path, "'" + where + "' must be a string");
        if (std::none_of(param.enum_values.begin(), param.enum_values.end(),
                         [&](const CpuConfigEnumValue& e) { return e.id == value.get<std::string>(); }))
            CfgFatal(path, "'" + where + "' has the unknown value '" + value.get<std::string>() + "'");
        break;
    }
    return ValueOf(param, value);
}

}
