#include "philips_velo_1_rom_upgrade_card.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/host_file_bytes.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/hibernation.h>
#include "../../socs/pr31x00/pr31x00_chip_selects.h"
#include "philips_velo_1_family_id.h"

#include <algorithm>
#include <string>

namespace cerfcore {

REGISTER_SERVICE(PhilipsVelo1RomUpgradeCard);

bool PhilipsVelo1RomUpgradeCard::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
}

void PhilipsVelo1RomUpgradeCard::OnReady() {
    const DeviceConfig& cfg = emu_.Get<DeviceConfig>();
    if (cfg.rom_velo1_rom_upgrade_card.empty()) return;

    const std::string path = ResolveDeviceFile(cfg.device_name, cfg.rom_velo1_rom_upgrade_card);
    std::vector<uint8_t> bytes;
    if (!TryReadHostFileBytes(path, bytes)) {
        emu_.Get<Fatal>().UserError("ROM upgrade Miniature Card image %s cannot be read",
                                    path.c_str());
    }
    constexpr uint32_t kWindow = kPr31x00Cs1KusegEnd - kPr31x00Cs1KusegBase;
    if (bytes.empty() || bytes.size() > kWindow) {
        emu_.Get<Fatal>().UserError("ROM upgrade Miniature Card image %s is %zu bytes; it must "
                                    "hold 1 to %u bytes", path.c_str(), bytes.size(), kWindow);
    }
    bytes_ = std::move(bytes);
    emu_.Get<Hibernation>().RegisterRomImage(bytes_.size());
    LOG(Boot, "PhilipsVelo1RomUpgradeCard: %s, %zu bytes in slot 2\n", path.c_str(),
        bytes_.size());
}

Velo1CardWindow PhilipsVelo1RomUpgradeCard::KsegWindow() const {
    const size_t window = kPr31x00Cs1KsegEnd - kPr31x00Cs1KsegBase;
    return { kPr31x00Cs1KsegBase,
             std::span<const uint8_t>(bytes_).first(std::min(bytes_.size(), window)) };
}

Velo1CardWindow PhilipsVelo1RomUpgradeCard::KusegWindow() const {
    return { kPr31x00Cs1KusegBase, bytes_ };
}

}
