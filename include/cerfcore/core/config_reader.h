#pragma once

#include "config_json.h"
#include "log.h"
#include "service.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cerfcore {

class ConfigReader : public Service {
public:
    using Service::Service;

    [[noreturn]] void Fail(const std::string& path, const std::string& msg);

    template <class JsonT = nlohmann::json>
    JsonT ReadJsonFile(const std::string& path, CfgBrokenFile broken = CfgBrokenFile::kHalt) {
        std::string        text;
        const HostFileRead read = CfgReadTextFile(path, text);
        if (read == HostFileRead::kAbsent) return JsonT();
        std::string error = "cannot be read";
        if (read == HostFileRead::kRead) {
            std::optional<JsonT> j = CfgParseJsonObject<JsonT>(text, error);
            if (j) return std::move(*j);
        }
        if (broken == CfgBrokenFile::kHalt) Fail(path, error);
        LOG(Cfg, "%s %s; not read\n", path.c_str(), error.c_str());
        return JsonT::object();
    }

    template <class JsonT = nlohmann::json>
    JsonT ReadLoggedJsonFile(const std::string& path, CfgBrokenFile broken = CfgBrokenFile::kHalt) {
        JsonT j = ReadJsonFile<JsonT>(path, broken);
        if (j.is_null()) {
            LOG(Cfg, "%s: absent\n", path.c_str());
            return j;
        }
        constexpr size_t kChunk = 1000;
        const std::string text = j.dump();
        LOG(Cfg, "%s:\n", path.c_str());
        for (size_t start = 0; start < text.size(); start += kChunk)
            LOG(Cfg, "  %s\n", text.substr(start, kChunk).c_str());
        return j;
    }

    std::vector<std::string> ReadStringArray(const nlohmann::json& list, const std::string& path,
                                             const std::string& ctx);
    const nlohmann::json*    ReadOptObjectArray(const nlohmann::json& obj, const char* key, const std::string& path,
                                                const std::string& ctx);
    std::string              ReadOptString(const nlohmann::json& obj, const char* key, const std::string& path,
                                           const std::string& ctx);
    int                      ReadOptInt(const nlohmann::json& obj, const char* key, const std::string& path,
                                        const std::string& ctx);
    bool                     LoadOptBool(const nlohmann::json& obj, const char* key, bool& target,
                                         const std::string& path, const std::string& ctx);
    bool                     ReadOptBool(const nlohmann::json& obj, const char* key, const std::string& path,
                                         const std::string& ctx);
};

}
