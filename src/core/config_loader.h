#pragma once
#include <cerfcore/core/service.h>
#include <cerfcore/core/device_config.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace cerfcore {

class ConfigReader;

class ConfigLoader : public Service {
public:
    using Service::Service;

    void LoadInto(DeviceConfig& config);

    void ApplyBoardPanelDefault(DeviceConfig& config);

private:
    ConfigReader& Reader();

    void SetNonEmptyString(std::string& field, const nlohmann::json& obj, const char* key, const std::string& path,
                           const std::string& ctx);
    void SetMetaInt(int& field, const nlohmann::json& obj, const char* key, const std::string& path,
                    const std::string& ctx);
    void LoadMeta(const nlohmann::json& root, DeviceMeta& meta, const std::string& path);
    void LoadBoard(const nlohmann::json& root, DeviceConfig& config, const std::string& path);
    void LoadGaAutorun(const nlohmann::json& ga, DeviceConfig& config, const std::string& path);
    void LoadFeatures(const nlohmann::json& root, DeviceConfig& config, const std::string& path);
    void LoadNetwork(const nlohmann::json& root, DeviceConfig& config, const std::string& path);
    void LoadRom(const nlohmann::json& root, DeviceConfig& config, const std::string& path);
    void LoadStorage(const nlohmann::json& root, DeviceConfig& config, const std::string& path);
    template <class Card>
    Card ReadBundledImage(const nlohmann::json& e, const std::string& entry, const std::string& path);
    void LoadUsbMedia(const nlohmann::json& packages, const char* key, bool sd, std::vector<BundledUsbMedia>& cards,
                      const std::string& path);
    void LoadAdditionalPackages(const nlohmann::json& root, DeviceConfig& config, const std::string& path);
};

}
