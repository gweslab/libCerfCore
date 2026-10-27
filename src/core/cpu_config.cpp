#include "cpu_config.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/device_config_keys.h>
#include <cerfcore/core/log.h>
#include "no_emulation_runtime_service.h"

#include <nlohmann/json.hpp>

namespace cerfcore {

REGISTER_SERVICE(CpuConfig);

void CpuConfig::OnReady() {
    emu_.Get<NoEmulationRuntimeService>().EnsureEmulationPrevented();
    const auto soc_id = emu_.Get<BoardContext>().GetSocId();
    values_ = emu_.Get<CpuConfigDatabase>().ResolvePreset(soc_id);
    const std::string db_path = DbJsonPath();
    for (const auto& [id, value] : values_) origins_[id] = db_path;
    ApplyUserOverrides();
}

void CpuConfig::ApplyUserOverrides() {
    const std::string path = UserConfigPath(emu_.Get<DeviceConfig>().device_name);
    const nlohmann::json root = CfgReadJsonFile(path, CfgBrokenFile::kSkip);
    if (root.is_null() || !root.contains(config_key::kCpuConfig)) return;
    const auto& overrides = root[config_key::kCpuConfig];
    if (!overrides.is_object())
        CfgFatal(path, std::string("'") + config_key::kCpuConfig + "' must be an object");

    const auto& db   = emu_.Get<CpuConfigDatabase>();
    const auto  arch = emu_.Get<BoardContext>().GetCpuArch();
    for (const auto& [id, value] : overrides.items()) {
        const std::string     key   = CfgKeyPath({config_key::kCpuConfig, id});
        const CpuConfigParam* param = db.FindParam(id);
        if (!param || param->arch != arch) {
            LOG(Cfg, "%s does not apply to this board\n", key.c_str());
            continue;
        }
        values_[id]  = db.ParseValue(*param, value, path, config_key::kCpuConfig);
        origins_[id] = path;
        LOG(Cfg, "%s overridden by %s\n", key.c_str(), path.c_str());
    }
}

bool CpuConfig::Boolean(std::string_view id) const { return values_.find(id)->second.boolean; }

uint32_t CpuConfig::Number(std::string_view id) const { return values_.find(id)->second.number; }

const std::string& CpuConfig::EnumId(std::string_view id) const { return values_.find(id)->second.enum_id; }

const std::string& CpuConfig::Origin(std::string_view id) const { return origins_.find(id)->second; }

}
