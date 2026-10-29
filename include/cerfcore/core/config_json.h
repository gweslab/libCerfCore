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

nlohmann::json CfgReadInstalledJsonFile(const std::string& path);

}
