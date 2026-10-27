#include <cerfcore/core/cerf_paths.h>

#include <cerfcore/core/host_fs_path.h>
#include <cerfcore/core/path_split.h>

#include "../platform/host_document_path.h"
#include "../platform/host_install_directory.h"

#include <filesystem>

namespace cerfcore {

namespace {

constexpr char kSeparator = static_cast<char>(std::filesystem::path::preferred_separator);
constexpr char kHostSeparators[] = {kSeparator, '/', '\0'};

}

std::string GetCerfDir() { return host_platform::InstallDirectory(); }

std::string DbJsonPath() { return GetCerfDir() + kDbJsonFileName; }

std::string DevicesDir() { return GetCerfDir() + kDevicesDirName + kSeparator; }

std::string GetDeviceDir(const std::string& device_name) { return DevicesDir() + device_name + kSeparator; }

std::string CeAppPath(const std::string& arch_dir, const std::string& file) {
    return GetCerfDir() + kCeAppsDirName + kSeparator + arch_dir + kSeparator + file;
}

std::string UserConfigPath(const std::string& device_name) {
    return GetDeviceDir(device_name) + kUserConfigFileName;
}

bool IsHostDocumentPath(const std::string& path) { return host_platform::IsHostDocument(path); }

bool IsAbsoluteHostPath(const std::string& path) {
    return IsHostDocumentPath(path) || HostFsPath(path).has_root_path();
}

std::string ResolveFileIn(const std::string& folder, const std::string& filename) {
    if (IsAbsoluteHostPath(filename)) return filename;
    return AppendPathName(folder, filename, kHostSeparators);
}

std::string ResolveDeviceFile(const std::string& device_name, const std::string& filename) {
    return ResolveFileIn(GetDeviceDir(device_name), filename);
}

}
