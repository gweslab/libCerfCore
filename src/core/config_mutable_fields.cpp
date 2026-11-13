#include "config_mutable_fields.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/config_reader.h>
#include <cerfcore/core/device_config_keys.h>
#include <cerfcore/core/ga_palette_json.h>
#include <cerfcore/core/share_folder_mount_name.h>

#include <optional>
#include <utility>
#include <vector>

namespace cerfcore {

REGISTER_SERVICE(ConfigMutableFields);

void CfgResetMutableFields(DeviceConfig& config) {
    config.board_configurable_screen_width    = kDefaultConfigurableScreenWidth;
    config.board_configurable_screen_height   = kDefaultConfigurableScreenHeight;
    config.board_configurable_screen_explicit = false;
    config.screen_dpi                         = 0;
    config.board_configurable_screen_bpp      = 0;
    config.guest_additions_color_scheme.clear();
    config.guest_additions_font_size = 0;
    config.guest_additions_font_size_set = false;
    config.guest_additions_cleartype     = false;
    config.guest_additions_cleartype_set = false;
    config.guest_additions_allow_untrusted_apps = kDefaultAllowUntrustedApps;
    config.guest_additions_palette.clear();
    config.share_folder.clear();
    config.share_folder_mount_point.clear();
    config.hud.clear();
    config.shader_presets.clear();
    config.shader_parameters.clear();
    config.shader_screenshots = kDefaultShaderScreenshots;
}

void ConfigMutableFields::LoadScreenFields(const nlohmann::json& board, DeviceConfig& config,
                                           const std::string& path) {
    auto& reader = emu_.Get<ConfigReader>();
    if (board.contains(config_key::kScreenWidth)) {
        const int n = reader.ReadOptInt(board, config_key::kScreenWidth, path, config_key::kBoard);
        if (n < 1)
            reader.Fail(path, "'" + CfgKeyPath({config_key::kBoard, config_key::kScreenWidth}) + "' must be >= 1");
        config.board_configurable_screen_width    = (uint32_t)n;
        config.board_configurable_screen_explicit = true;
    }
    if (board.contains(config_key::kScreenHeight)) {
        const int n = reader.ReadOptInt(board, config_key::kScreenHeight, path, config_key::kBoard);
        if (n < 1)
            reader.Fail(path, "'" + CfgKeyPath({config_key::kBoard, config_key::kScreenHeight}) + "' must be >= 1");
        config.board_configurable_screen_height   = (uint32_t)n;
        config.board_configurable_screen_explicit = true;
    }
    if (board.contains(config_key::kScreenDpi)) {
        const int n = reader.ReadOptInt(board, config_key::kScreenDpi, path, config_key::kBoard);
        if (n < 1)
            reader.Fail(path, "'" + CfgKeyPath({config_key::kBoard, config_key::kScreenDpi}) + "' must be >= 1");
        config.screen_dpi = (uint32_t)n;
    }
    if (board.contains(config_key::kScreenBpp)) {
        const int n = reader.ReadOptInt(board, config_key::kScreenBpp, path, config_key::kBoard);
        if (n < 1)
            reader.Fail(path, "'" + CfgKeyPath({config_key::kBoard, config_key::kScreenBpp}) + "' must be >= 1");
        config.board_configurable_screen_bpp = (uint32_t)n;
    }
}

void ConfigMutableFields::LoadShareFolder(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (!ga.contains(config_key::kShareFolder)) return;
    const auto& v = ga[config_key::kShareFolder];
    if (v.is_string())
        config.share_folder = v.get<std::string>();
    else if (!v.is_null())
        emu_.Get<ConfigReader>().Fail(path, "'" + CfgKeyPath({config_key::kGuestAdditions, config_key::kShareFolder}) +
                                                "' must be a string (or null)");
}

void ConfigMutableFields::LoadShareFolderMountPoint(const nlohmann::json& ga, DeviceConfig& config,
                                                    const std::string& path) {
    if (!ga.contains(config_key::kShareFolderMountPoint)) return;
    const auto& v = ga[config_key::kShareFolderMountPoint];
    if (v.is_null()) return;
    if (!v.is_string() || !IsValidShareFolderMountName(v.get<std::string>()))
        emu_.Get<ConfigReader>().Fail(
            path, "'" + CfgKeyPath({config_key::kGuestAdditions, config_key::kShareFolderMountPoint}) +
                      "' must be a valid mount point name (or null)");
    config.share_folder_mount_point = v.get<std::string>();
}

void ConfigMutableFields::LoadColorScheme(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (!ga.contains(config_key::kColorScheme)) return;
    const auto& cs = ga[config_key::kColorScheme];
    if (cs.is_string())
        config.guest_additions_color_scheme = cs.get<std::string>();
    else if (!cs.is_null())
        emu_.Get<ConfigReader>().Fail(path, "'" + CfgKeyPath({config_key::kGuestAdditions, config_key::kColorScheme}) +
                                                "' must be a string (or null)");
}

void ConfigMutableFields::LoadGaFontSize(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (!ga.contains(config_key::kFontSize)) return;
    if (ga[config_key::kFontSize].is_null()) return;
    config.guest_additions_font_size =
        (int32_t)emu_.Get<ConfigReader>().ReadOptInt(ga, config_key::kFontSize, path, config_key::kGuestAdditions);
    config.guest_additions_font_size_set = true;
}

void ConfigMutableFields::LoadGaClearType(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (emu_.Get<ConfigReader>().LoadOptBool(ga, config_key::kClearType, config.guest_additions_cleartype, path,
                                             config_key::kGuestAdditions))
        config.guest_additions_cleartype_set = true;
}

void ConfigMutableFields::LoadGaAllowUntrustedApps(const nlohmann::json& ga, DeviceConfig& config,
                                                   const std::string& path) {
    emu_.Get<ConfigReader>().LoadOptBool(ga, config_key::kAllowUntrustedApps, config.guest_additions_allow_untrusted_apps,
                                         path, config_key::kGuestAdditions);
}

void ConfigMutableFields::LoadGaPalette(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (!ga.contains(config_key::kIndexedPalette)) return;
    const auto& v = ga[config_key::kIndexedPalette];
    if (v.is_null()) return;
    std::optional<std::vector<uint32_t>> colors = GaPaletteFromJson(v);
    if (!colors)
        emu_.Get<ConfigReader>().Fail(
            path, "'" + CfgKeyPath({config_key::kGuestAdditions, config_key::kIndexedPalette}) +
                      "' must be an array of up to 256 [red, green, blue] colors with values from 0 to 255 "
                      "(or null)");
    config.guest_additions_palette = std::move(*colors);
}

void ConfigMutableFields::LoadGaFields(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    LoadColorScheme(ga, config, path);
    LoadGaFontSize(ga, config, path);
    LoadGaClearType(ga, config, path);
    LoadGaAllowUntrustedApps(ga, config, path);
    LoadGaPalette(ga, config, path);
    LoadShareFolder(ga, config, path);
    LoadShareFolderMountPoint(ga, config, path);
}

void ConfigMutableFields::LoadHud(const nlohmann::json& root, DeviceConfig& config, const std::string& path) {
    if (root.contains(config_key::kHud))
        config.hud = emu_.Get<ConfigReader>().ReadOptString(root, config_key::kHud, path, {});
}

void ConfigMutableFields::LoadShaders(const nlohmann::json& root, DeviceConfig& config, const std::string& path) {
    if (!root.contains(config_key::kShaders)) return;
    auto&       reader  = emu_.Get<ConfigReader>();
    const auto& shaders = root[config_key::kShaders];
    if (!shaders.is_object())
        reader.Fail(path, std::string("'") + config_key::kShaders + "' must be an object");
    config.shader_presets.clear();
    config.shader_parameters.clear();
    config.shader_screenshots = kDefaultShaderScreenshots;
    reader.LoadOptBool(shaders, config_key::kShaderScreenshots, config.shader_screenshots, path, config_key::kShaders);
    if (const nlohmann::json* presets =
            reader.ReadOptObjectArray(shaders, config_key::kShaderPresets, path, config_key::kShaders)) {
        const std::string entry = CfgElementPath(CfgKeyPath({config_key::kShaders, config_key::kShaderPresets}));
        for (const auto& preset : *presets) {
            const std::string preset_path = reader.ReadOptString(preset, config_key::kShaderPath, path, entry);
            if (preset_path.empty())
                reader.Fail(path, "'" + CfgKeyPath({entry, config_key::kShaderPath}) + "' must be a preset path");
            bool enabled = kDefaultShaderPresetEnabled;
            reader.LoadOptBool(preset, config_key::kShaderEnabled, enabled, path, entry);
            config.shader_presets.push_back(ShaderPresetEntry{preset_path, enabled});
        }
    }
    if (!shaders.contains(config_key::kShaderParameters)) return;
    const auto& parameters = shaders[config_key::kShaderParameters];
    const std::string key  = CfgKeyPath({config_key::kShaders, config_key::kShaderParameters});
    if (!parameters.is_object()) reader.Fail(path, "'" + key + "' must be an object of numbers");
    for (const auto& [name, value] : parameters.items()) {
        if (!value.is_number()) reader.Fail(path, "'" + key + "." + name + "' must be a number");
        config.shader_parameters.emplace_back(name, value.get<float>());
    }
}

void ConfigMutableFields::LoadAll(const nlohmann::json& root, DeviceConfig& config, const std::string& path) {
    LoadHud(root, config, path);
    LoadShaders(root, config, path);
    if (root.contains(config_key::kBoard)) {
        const auto& b = root[config_key::kBoard];
        if (!b.is_object())
            emu_.Get<ConfigReader>().Fail(path, std::string("'") + config_key::kBoard + "' must be an object");
        LoadScreenFields(b, config, path);
    }
    if (root.contains(config_key::kGuestAdditions)) {
        const auto& ga = root[config_key::kGuestAdditions];
        if (ga.is_object()) LoadGaFields(ga, config, path);
    }
}

}
