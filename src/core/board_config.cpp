#include "board_config.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/config_reader.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/device_config_keys.h>
#include <cerfcore/core/log.h>
#include "no_emulation_runtime_service.h"

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(BoardConfig);

namespace {

std::string ValueList(const std::vector<std::string>& values) {
    std::string out;
    for (const std::string& v : values) out += (out.empty() ? "" : ", ") + v;
    return out;
}

}

void BoardConfig::OnReady() {
    emu_.Get<NoEmulationRuntimeService>().EnsureEmulationPrevented();
    const DbBoard& board = *emu_.Get<BoardDatabase>().FindBoard(emu_.Get<BoardContext>().GetBoardId());
    for (const DbBoardConfigSetting& s : board.config) values_[s.param_id] = s.default_value;

    for (const auto& [id, entry] : emu_.Get<DeviceConfig>().board_config) {
        const std::string           key     = CfgKeyPath({config_key::kBoard, config_key::kConfig, id});
        const DbBoardConfigSetting* setting = FindBoardConfigSetting(board.config, id);
        if (!setting) {
            LOG(Cfg, "%s does not apply to this board\n", key.c_str());
            continue;
        }
        if (!setting->Accepts(entry.value))
            emu_.Get<ConfigReader>().Fail(entry.path, "'" + key + "' must be one of " + ValueList(setting->values));
        values_[id] = entry.value;
        LOG(Cfg, "%s = %s from %s\n", key.c_str(), entry.value.c_str(), entry.path.c_str());
    }
}

const std::string& BoardConfig::EnumId(std::string_view param_id) const { return values_.find(param_id)->second; }

}
