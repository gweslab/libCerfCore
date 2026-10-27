#include "config_mutable_fields.h"

#include <cerfcore/core/config_json.h>
#include <cerfcore/core/device_config_keys.h>
#include <cerfcore/core/share_folder_mount_name.h>

namespace cerfcore {

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
    config.share_folder.clear();
    config.share_folder_mount_point.clear();
}

void CfgLoadMutableScreenFields(const nlohmann::json& board, DeviceConfig& config, const std::string& path) {
    if (board.contains(config_key::kScreenWidth)) {
        const int n = CfgReadOptInt(board, config_key::kScreenWidth, path, config_key::kBoard);
        if (n < 1)
            CfgFatal(path, "'" + CfgKeyPath({config_key::kBoard, config_key::kScreenWidth}) + "' must be >= 1");
        config.board_configurable_screen_width    = (uint32_t)n;
        config.board_configurable_screen_explicit = true;
    }
    if (board.contains(config_key::kScreenHeight)) {
        const int n = CfgReadOptInt(board, config_key::kScreenHeight, path, config_key::kBoard);
        if (n < 1)
            CfgFatal(path, "'" + CfgKeyPath({config_key::kBoard, config_key::kScreenHeight}) + "' must be >= 1");
        config.board_configurable_screen_height   = (uint32_t)n;
        config.board_configurable_screen_explicit = true;
    }
    if (board.contains(config_key::kScreenDpi)) {
        const int n = CfgReadOptInt(board, config_key::kScreenDpi, path, config_key::kBoard);
        if (n < 1)
            CfgFatal(path, "'" + CfgKeyPath({config_key::kBoard, config_key::kScreenDpi}) + "' must be >= 1");
        config.screen_dpi = (uint32_t)n;
    }
    if (board.contains(config_key::kScreenBpp)) {
        const int n = CfgReadOptInt(board, config_key::kScreenBpp, path, config_key::kBoard);
        if (n < 1)
            CfgFatal(path, "'" + CfgKeyPath({config_key::kBoard, config_key::kScreenBpp}) + "' must be >= 1");
        config.board_configurable_screen_bpp = (uint32_t)n;
    }
}

void CfgLoadShareFolder(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (!ga.contains(config_key::kShareFolder)) return;
    const auto& v = ga[config_key::kShareFolder];
    if (v.is_string())
        config.share_folder = v.get<std::string>();
    else if (!v.is_null())
        CfgFatal(path, "'" + CfgKeyPath({config_key::kGuestAdditions, config_key::kShareFolder}) +
                           "' must be a string (or null)");
}

void CfgLoadShareFolderMountPoint(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (!ga.contains(config_key::kShareFolderMountPoint)) return;
    const auto& v = ga[config_key::kShareFolderMountPoint];
    if (v.is_null()) return;
    if (!v.is_string() || !IsValidShareFolderMountName(v.get<std::string>()))
        CfgFatal(path, "'" + CfgKeyPath({config_key::kGuestAdditions, config_key::kShareFolderMountPoint}) +
                           "' must be a valid mount point name (or null)");
    config.share_folder_mount_point = v.get<std::string>();
}

void CfgLoadColorScheme(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (!ga.contains(config_key::kColorScheme)) return;
    const auto& cs = ga[config_key::kColorScheme];
    if (cs.is_string())
        config.guest_additions_color_scheme = cs.get<std::string>();
    else if (!cs.is_null())
        CfgFatal(path, "'" + CfgKeyPath({config_key::kGuestAdditions, config_key::kColorScheme}) +
                           "' must be a string (or null)");
}

void CfgLoadGaFontSize(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (!ga.contains(config_key::kFontSize)) return;
    if (ga[config_key::kFontSize].is_null()) return;
    config.guest_additions_font_size =
        (int32_t)CfgReadOptInt(ga, config_key::kFontSize, path, config_key::kGuestAdditions);
    config.guest_additions_font_size_set = true;
}

void CfgLoadGaClearType(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    if (CfgLoadOptBool(ga, config_key::kClearType, config.guest_additions_cleartype, path,
                       config_key::kGuestAdditions))
        config.guest_additions_cleartype_set = true;
}

void CfgLoadGaAllowUntrustedApps(const nlohmann::json& ga, DeviceConfig& config, const std::string& path) {
    CfgLoadOptBool(ga, config_key::kAllowUntrustedApps, config.guest_additions_allow_untrusted_apps, path,
                   config_key::kGuestAdditions);
}

void CfgLoadMutableFields(const nlohmann::json& root, DeviceConfig& config, const std::string& path) {
    if (root.contains(config_key::kBoard)) {
        const auto& b = root[config_key::kBoard];
        if (!b.is_object()) CfgFatal(path, std::string("'") + config_key::kBoard + "' must be an object");
        CfgLoadMutableScreenFields(b, config, path);
    }
    if (root.contains(config_key::kGuestAdditions)) {
        const auto& ga = root[config_key::kGuestAdditions];
        if (ga.is_object()) {
            CfgLoadColorScheme(ga, config, path);
            CfgLoadGaFontSize(ga, config, path);
            CfgLoadGaClearType(ga, config, path);
            CfgLoadGaAllowUntrustedApps(ga, config, path);
            CfgLoadShareFolder(ga, config, path);
            CfgLoadShareFolderMountPoint(ga, config, path);
        }
    }
}

}
