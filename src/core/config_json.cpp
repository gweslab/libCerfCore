#include <cerfcore/core/config_json.h>

#include <cerfcore/platform/host_error.h>
#include <cerfcore/platform/host_file.h>

namespace cerfcore {

void CfgFatal(const std::string& path, const std::string& msg) {
    LOG(Caution, "FATAL: '%s' %s\n", path.c_str(), msg.c_str());
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

nlohmann::json CfgReadInstalledJsonFile(const std::string& path) {
    std::string text;
    CfgReadTextFile(path, text);
    return nlohmann::json::parse(text);
}

std::string CfgReadOptString(const nlohmann::json& obj, const char* key, const std::string& path,
                             const std::string& ctx) {
    if (!obj.contains(key)) return {};
    const auto& v = obj[key];
    if (v.is_null()) return {};
    if (!v.is_string()) CfgFatal(path, "'" + CfgKeyPath({ctx, key}) + "' must be a string (or null)");
    return v.get<std::string>();
}

int CfgReadOptInt(const nlohmann::json& obj, const char* key, const std::string& path, const std::string& ctx) {
    if (!obj.contains(key)) return 0;
    const auto& v = obj[key];
    if (v.is_null()) return 0;
    if (!v.is_number_integer()) CfgFatal(path, "'" + CfgKeyPath({ctx, key}) + "' must be an integer");
    return v.get<int>();
}

bool CfgLoadOptBool(const nlohmann::json& obj, const char* key, bool& target, const std::string& path,
                    const std::string& ctx) {
    if (!obj.contains(key)) return false;
    const auto& v = obj[key];
    if (v.is_null()) return false;
    if (!v.is_boolean()) CfgFatal(path, "'" + CfgKeyPath({ctx, key}) + "' must be a boolean (or null)");
    target = v.get<bool>();
    return true;
}

bool CfgReadOptBool(const nlohmann::json& obj, const char* key, const std::string& path, const std::string& ctx) {
    bool value = false;
    CfgLoadOptBool(obj, key, value, path, ctx);
    return value;
}

std::string CfgKeyPath(std::initializer_list<std::string_view> keys) {
    std::string out;
    for (const std::string_view key : keys) {
        if (key.empty()) continue;
        if (!out.empty()) out += '.';
        out += key;
    }
    return out;
}

std::string CfgElementPath(std::string_view array_path) {
    std::string out(array_path);
    out += "[]";
    return out;
}

const nlohmann::json* CfgReadOptObjectArray(const nlohmann::json& obj, const char* key, const std::string& path,
                                            const std::string& ctx) {
    if (!obj.contains(key) || obj[key].is_null()) return nullptr;
    const std::string key_path = CfgKeyPath({ctx, key});
    const auto&       list     = obj[key];
    if (!list.is_array()) CfgFatal(path, "'" + key_path + "' must be an array");
    for (const auto& entry : list)
        if (!entry.is_object()) CfgFatal(path, "'" + CfgElementPath(key_path) + "' entries must be objects");
    return &list;
}

HostFileRead CfgReadTextFile(const std::string& path, std::string& text) {
    std::vector<uint8_t> bytes;
    const HostFileRead   read = ReadHostFileIfPresent(path, bytes);
    if (read == HostFileRead::kRead) text.assign(bytes.begin(), bytes.end());
    return read;
}

bool CfgWriteJsonFile(const std::string& path, const nlohmann::ordered_json& value, uint32_t& error) {
    return host_platform::ReplaceHostFileContents(path, value.dump(2) + "\n", true, error);
}

bool CfgUpdateJsonFile(const std::string& path, const std::function<void(nlohmann::ordered_json&)>& mutate,
                       CfgEmptyObject empty, std::string& error) {
    nlohmann::ordered_json object = nlohmann::ordered_json::object();
    std::string            text;
    const HostFileRead     read = CfgReadTextFile(path, text);
    if (read != HostFileRead::kAbsent) {
        std::string                           problem = "cannot be read";
        std::optional<nlohmann::ordered_json> parsed;
        if (read == HostFileRead::kRead) parsed = CfgParseJsonObject<nlohmann::ordered_json>(text, problem);
        if (parsed) object = std::move(*parsed);
        else        LOG(Cfg, "%s %s; it is overwritten\n", path.c_str(), problem.c_str());
    }
    mutate(object);
    uint32_t   code = 0;
    const bool done = object.empty() && empty == CfgEmptyObject::kRemoveFile
                          ? host_platform::DeleteHostFile(path, code) != host_platform::DeleteResult::kFailed
                          : CfgWriteJsonFile(path, object, code);
    if (!done) error = path + ": " + host_platform::ErrorText(code);
    return done;
}

std::vector<std::string> CfgReadStringArray(const nlohmann::json& list, const std::string& path,
                                            const std::string& ctx) {
    if (!list.is_array()) CfgFatal(path, "'" + ctx + "' must be an array");
    std::vector<std::string> out;
    for (const auto& v : list) {
        if (!v.is_string() || v.get<std::string>().empty())
            CfgFatal(path, "'" + CfgElementPath(ctx) + "' entries must be non-empty strings");
        out.push_back(v.get<std::string>());
    }
    return out;
}

}
