#pragma once

#include <cerfcore/core/device_config.h>

#include <nlohmann/json.hpp>

#include <string>

namespace cerfcore {

void CfgResetMutableFields(DeviceConfig& config);
void CfgLoadMutableScreenFields(const nlohmann::json& board, DeviceConfig& config, const std::string& path);
void CfgLoadShareFolder(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
void CfgLoadShareFolderMountPoint(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
void CfgLoadColorScheme(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
void CfgLoadGaFontSize(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
void CfgLoadGaClearType(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
void CfgLoadGaAllowUntrustedApps(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
void CfgLoadMutableFields(const nlohmann::json& root, DeviceConfig& config, const std::string& path);

}
