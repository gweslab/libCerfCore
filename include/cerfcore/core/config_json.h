#pragma once

#include "host_file_read.h"
#include "log.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

[[noreturn]] void CfgFatal(const std::string& path, const std::string& msg);

std::string CfgKeyPath(std::initializer_list<std::string_view> keys);
std::string CfgElementPath(std::string_view array_path);

HostFileRead CfgReadTextFile(const std::string& path, std::string& text);
bool         CfgWriteJsonFile(const std::string& path, const nlohmann::ordered_json& value, uint32_t& error);

enum class CfgEmptyObject : uint8_t {
    kWrite,
    kRemoveFile,
};

bool CfgUpdateJsonFile(const std::string& path, const std::function<void(nlohmann::ordered_json&)>& mutate,
                       CfgEmptyObject empty, std::string& error);

std::vector<std::string> CfgReadStringArray(const nlohmann::json& list, const std::string& path,
                                            const std::string& ctx);
const nlohmann::json* CfgReadOptObjectArray(const nlohmann::json& obj, const char* key, const std::string& path,
                                            const std::string& ctx);

template <class JsonT = nlohmann::json>
std::optional<JsonT> CfgParseJsonObject(std::string_view text, std::string& error) {
    JsonT j;
    try {
        j = JsonT::parse(text.begin(), text.end());
    } catch (const std::exception& e) {
        error = std::string("is not valid JSON: ") + e.what();
        return std::nullopt;
    }
    if (!j.is_object()) {
        error = "is not a JSON object";
        return std::nullopt;
    }
    return j;
}

enum class CfgBrokenFile : uint8_t {
    kHalt,
    kSkip,
};

template <class JsonT = nlohmann::json>
JsonT CfgReadJsonFile(const std::string& path, CfgBrokenFile broken = CfgBrokenFile::kHalt) {
    std::string        text;
    const HostFileRead read = CfgReadTextFile(path, text);
    if (read == HostFileRead::kAbsent) return JsonT();
    std::string error = "cannot be read";
    if (read == HostFileRead::kRead) {
        std::optional<JsonT> j = CfgParseJsonObject<JsonT>(text, error);
        if (j) return std::move(*j);
    }
    if (broken == CfgBrokenFile::kHalt) CfgFatal(path, error);
    LOG(Cfg, "%s %s; not read\n", path.c_str(), error.c_str());
    return JsonT::object();
}

template <class JsonT = nlohmann::json>
JsonT CfgReadLoggedJsonFile(const std::string& path, CfgBrokenFile broken = CfgBrokenFile::kHalt) {
    JsonT j = CfgReadJsonFile<JsonT>(path, broken);
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

nlohmann::json CfgReadInstalledJsonFile(const std::string& path);
std::string    CfgReadOptString(const nlohmann::json& obj, const char* key, const std::string& path,
                                const std::string& ctx);
int            CfgReadOptInt(const nlohmann::json& obj, const char* key, const std::string& path,
                             const std::string& ctx);
bool           CfgLoadOptBool(const nlohmann::json& obj, const char* key, bool& target, const std::string& path,
                              const std::string& ctx);
bool           CfgReadOptBool(const nlohmann::json& obj, const char* key, const std::string& path,
                              const std::string& ctx);

}
