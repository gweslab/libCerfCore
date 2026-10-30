#pragma once

#include "install_layout_text.h"

#include <string>

namespace cerfcore {

inline constexpr char kDbJsonFileName[]       = CERF_DB_JSON_NAME;
inline constexpr char kHudsJsonFileName[]     = CERF_HUDS_JSON_NAME;
inline constexpr char kCeAppsDirName[]        = CERF_CE_APPS_DIR_NAME;
inline constexpr char kDevicesDirName[]       = "devices";
inline constexpr char kDeviceConfigFileName[] = "cerf.json";
inline constexpr char kUserConfigFileName[]   = "cerf-user.json";
inline constexpr char kLogFileName[]          = "cerf.log";
inline constexpr char kCrashLogFileName[]     = "cerf.crash.log";
inline constexpr char kDefaultCfImageName[]   = "CF.IMG";

std::string GetCerfDir();
std::string DbJsonPath();
std::string HudsJsonPath();
std::string DevicesDir();
std::string GetDeviceDir(const std::string& device_name);
std::string CeAppPath(const std::string& arch_dir, const std::string& file);
std::string UserConfigPath(const std::string& device_name);
bool        IsHostDocumentPath(const std::string& path);
bool        IsAbsoluteHostPath(const std::string& path);
std::string ResolveFileIn(const std::string& folder, const std::string& filename);
std::string ResolveDeviceFile(const std::string& device_name, const std::string& filename);

}
