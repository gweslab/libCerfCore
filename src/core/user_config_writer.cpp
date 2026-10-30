#include <cerfcore/core/user_config_writer.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/device_config_keys.h>
#include <cerfcore/core/log.h>

namespace cerfcore {

REGISTER_SERVICE(UserConfigWriter);

void UserConfigWriter::WriteConfigurableScreenSize(uint32_t width,
                                                   uint32_t height) {
    if (width == 0 || height == 0) return;

    auto& config = emu_.Get<DeviceConfig>();
    const std::string path = UserConfigPath(config.device_name);

    std::string error;
    const bool  written = CfgUpdateJsonFile(
        path,
        [&](nlohmann::ordered_json& j) {
            if (!j.contains(config_key::kBoard) || !j[config_key::kBoard].is_object())
                j[config_key::kBoard] = nlohmann::ordered_json::object();
            j[config_key::kBoard][config_key::kScreenWidth]  = width;
            j[config_key::kBoard][config_key::kScreenHeight] = height;
        },
        CfgEmptyObject::kWrite, error);
    if (!written) {
        LOG(Cfg, "UserConfigWriter: %s\n", error.c_str());
        return;
    }

    config.board_configurable_screen_width    = width;
    config.board_configurable_screen_height   = height;
    config.board_configurable_screen_explicit = true;
    LOG(Cfg, "UserConfigWriter: persisted guest resolution %ux%u\n", width,
        height);
}

void UserConfigWriter::WriteHud(const std::string& hud) {
    auto&             config = emu_.Get<DeviceConfig>();
    const std::string path   = UserConfigPath(config.device_name);

    std::string error;
    const bool  written = CfgUpdateJsonFile(
        path,
        [&](nlohmann::ordered_json& j) {
            if (hud.empty()) j.erase(config_key::kHud);
            else             j[config_key::kHud] = hud;
        },
        CfgEmptyObject::kRemoveFile, error);
    if (!written) {
        LOG(Cfg, "UserConfigWriter: %s\n", error.c_str());
        return;
    }

    config.hud = hud;
    LOG(Cfg, "UserConfigWriter: persisted hud '%s'\n", hud.c_str());
}

}
