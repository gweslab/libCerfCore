#include <cerfcore/core/config_reader.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

REGISTER_SERVICE(ConfigReader);

void ConfigReader::Fail(const std::string& path, const std::string& msg) {
    emu_.Get<Fatal>().Die("FATAL: '%s' %s", path.c_str(), msg.c_str());
}

std::string ConfigReader::ReadOptString(const nlohmann::json& obj, const char* key, const std::string& path,
                                        const std::string& ctx) {
    if (!obj.contains(key)) return {};
    const auto& v = obj[key];
    if (v.is_null()) return {};
    if (!v.is_string()) Fail(path, "'" + CfgKeyPath({ctx, key}) + "' must be a string (or null)");
    return v.get<std::string>();
}

int ConfigReader::ReadOptInt(const nlohmann::json& obj, const char* key, const std::string& path,
                             const std::string& ctx) {
    if (!obj.contains(key)) return 0;
    const auto& v = obj[key];
    if (v.is_null()) return 0;
    if (!v.is_number_integer()) Fail(path, "'" + CfgKeyPath({ctx, key}) + "' must be an integer");
    return v.get<int>();
}

bool ConfigReader::LoadOptBool(const nlohmann::json& obj, const char* key, bool& target, const std::string& path,
                               const std::string& ctx) {
    if (!obj.contains(key)) return false;
    const auto& v = obj[key];
    if (v.is_null()) return false;
    if (!v.is_boolean()) Fail(path, "'" + CfgKeyPath({ctx, key}) + "' must be a boolean (or null)");
    target = v.get<bool>();
    return true;
}

bool ConfigReader::ReadOptBool(const nlohmann::json& obj, const char* key, const std::string& path,
                               const std::string& ctx) {
    bool value = false;
    LoadOptBool(obj, key, value, path, ctx);
    return value;
}

const nlohmann::json* ConfigReader::ReadOptObjectArray(const nlohmann::json& obj, const char* key,
                                                       const std::string& path, const std::string& ctx) {
    if (!obj.contains(key) || obj[key].is_null()) return nullptr;
    const std::string key_path = CfgKeyPath({ctx, key});
    const auto&       list     = obj[key];
    if (!list.is_array()) Fail(path, "'" + key_path + "' must be an array");
    for (const auto& entry : list)
        if (!entry.is_object()) Fail(path, "'" + CfgElementPath(key_path) + "' entries must be objects");
    return &list;
}

std::vector<std::string> ConfigReader::ReadStringArray(const nlohmann::json& list, const std::string& path,
                                                       const std::string& ctx) {
    if (!list.is_array()) Fail(path, "'" + ctx + "' must be an array");
    std::vector<std::string> out;
    for (const auto& v : list) {
        if (!v.is_string() || v.get<std::string>().empty())
            Fail(path, "'" + CfgElementPath(ctx) + "' entries must be non-empty strings");
        out.push_back(v.get<std::string>());
    }
    return out;
}

}
