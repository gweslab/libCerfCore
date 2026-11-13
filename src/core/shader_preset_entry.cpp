#include <cerfcore/core/shader_preset_entry.h>

namespace cerfcore {

std::vector<std::string> EnabledShaderPresets(const std::vector<ShaderPresetEntry>& entries) {
    std::vector<std::string> chain;
    for (const ShaderPresetEntry& entry : entries)
        if (entry.enabled) chain.push_back(entry.path);
    return chain;
}

}
