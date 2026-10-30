#pragma once

#include <cerfcore/core/device_config.h>
#include <cerfcore/core/service.h>

#include <nlohmann/json.hpp>

#include <string>

namespace cerfcore {

void CfgResetMutableFields(DeviceConfig& config);

class ConfigMutableFields : public Service {
public:
    using Service::Service;

    void LoadScreenFields(const nlohmann::json& board, DeviceConfig& config, const std::string& path);
    void LoadShareFolder(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
    void LoadShareFolderMountPoint(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
    void LoadColorScheme(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
    void LoadGaFontSize(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
    void LoadGaClearType(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
    void LoadGaAllowUntrustedApps(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
    void LoadHud(const nlohmann::json& root, DeviceConfig& config, const std::string& path);
    void LoadAll(const nlohmann::json& root, DeviceConfig& config, const std::string& path);
};

}
