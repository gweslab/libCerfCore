#include <cerfcore/core/config_json.h>

#include <cerfcore/platform/host_error.h>
#include <cerfcore/platform/host_file.h>

namespace cerfcore {

nlohmann::json CfgReadInstalledJsonFile(const std::string& path) {
    std::string text;
    CfgReadTextFile(path, text);
    return nlohmann::json::parse(text);
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

}
