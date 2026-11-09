#include <cerfcore/core/config_enum_value.h>

#include <nlohmann/json.hpp>

namespace cerfcore {

std::vector<ConfigEnumValue> ParseConfigEnumValues(const nlohmann::json& list) {
    std::vector<ConfigEnumValue> out;
    for (const auto& v : list) out.push_back(ConfigEnumValue{v.at("id").get<std::string>(), v.at("name").get<std::string>()});
    return out;
}

const ConfigEnumValue* FindConfigEnumValue(std::span<const ConfigEnumValue> values, std::string_view id) {
    for (const ConfigEnumValue& v : values)
        if (v.id == id) return &v;
    return nullptr;
}

}
