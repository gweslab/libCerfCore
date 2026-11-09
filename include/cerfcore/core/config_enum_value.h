#pragma once

#include <nlohmann/json_fwd.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

struct ConfigEnumValue {
    std::string id;
    std::string name;
};

std::vector<ConfigEnumValue> ParseConfigEnumValues(const nlohmann::json& list);

const ConfigEnumValue* FindConfigEnumValue(std::span<const ConfigEnumValue> values, std::string_view id);

}
