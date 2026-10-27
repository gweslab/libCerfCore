#pragma once

#include "board_database.h"
#include "service.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

namespace cpu_config_db_key {

inline constexpr char kParams[]  = "cpu_config_params";
inline constexpr char kConfigs[] = "cpu_configs";
inline constexpr char kType[]    = "type";

}

enum class CpuConfigParamType { Boolean, Integer, Hex, Enum };

struct CpuConfigEnumValue {
    std::string id;
    std::string name;
};

struct CpuConfigParam {
    std::string                     id;
    std::string                     name;
    CpuArch                         arch;
    CpuConfigParamType              type;
    std::vector<CpuConfigEnumValue> enum_values;
};

struct CpuConfigValue {
    bool        boolean = false;
    uint32_t    number  = 0;
    std::string enum_id;

    bool operator==(const CpuConfigValue&) const = default;
};

using CpuConfigValues = std::map<std::string, CpuConfigValue, std::less<>>;

bool ParseCpuConfigHex(std::string_view text, uint32_t& out);

struct CpuConfigPreset {
    std::string     id;
    std::string     soc_id;
    std::string     inherits;
    CpuConfigValues values;
};

class CpuConfigDatabase : public Service {
public:
    using Service::Service;

    void OnReady() override;

    std::span<const CpuConfigParam>  Params() const { return params_; }
    std::span<const CpuConfigPreset> Presets() const { return presets_; }

    const CpuConfigParam*  FindParam(std::string_view id) const;
    const CpuConfigPreset& SocPreset(std::string_view soc_id) const;

    CpuConfigValues ResolvePreset(std::string_view soc_id) const;
    CpuConfigValues Resolve(const CpuConfigPreset& preset) const;

    CpuConfigValue ParseValue(const CpuConfigParam& param, const nlohmann::json& value,
                              const std::string& path, const std::string& ctx) const;

private:
    const CpuConfigPreset& PresetById(std::string_view id) const;
    void                   Apply(const CpuConfigPreset& preset, CpuConfigValues& out) const;

    std::vector<CpuConfigParam>  params_;
    std::vector<CpuConfigPreset> presets_;
};

}
