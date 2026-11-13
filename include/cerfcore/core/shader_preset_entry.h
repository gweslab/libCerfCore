#pragma once

#include <string>
#include <vector>

namespace cerfcore {

inline constexpr bool kDefaultShaderPresetEnabled = true;

struct ShaderPresetEntry {
    std::string path;
    bool        enabled = kDefaultShaderPresetEnabled;
};

std::vector<std::string> EnabledShaderPresets(const std::vector<ShaderPresetEntry>& entries);

}
