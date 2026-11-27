#include "device_not_found_service.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config_keys.h>
#include <cerfcore/core/device_file_types.h>
#include <cerfcore/core/host_file_bytes.h>

#include "../boot/sec_flash.h"
#include <cerfcore/host/configuration_problem.h>
#include <cerfcore/platform/host_file.h>
#include "../socs/imx51/imx51_nand_store.h"
#include "../storage/nor_flash_store.h"

#include <string>

namespace cerfcore {

REGISTER_SERVICE(DeviceNotFoundService);

bool DeviceNotFoundService::ShouldRegister() { return !IsDevicePresent(); }

bool DeviceNotFoundService::IsDevicePresent() {
    const auto& cfg = emu_.Get<DeviceConfig>();
    if (!cfg.rom_primary.empty() &&
        host_platform::HostFileExists(ResolveDeviceFile(cfg.device_name, cfg.rom_primary)))
        return true;
    if (HasEmmcStore()) return true;
    if (auto* sf = emu_.TryGet<SecFlash>(); sf && sf->IsPresent()) return true;
    if (emu_.TryGet<NorFlashStore>()) return true;
    return emu_.TryGet<Imx51NandStore>() != nullptr;
}

bool DeviceNotFoundService::HasEmmcStore() {
    const auto& cfg = emu_.Get<DeviceConfig>();
    for (const DeviceFileType& type : emu_.Get<DeviceFileTypes>().ForBoard(cfg.board_id)) {
        if (type.section == DeviceFileSection::kStorage && type.id == config_key::kStorageEmmc)
            return HostFileNonEmpty(ResolveDeviceFile(cfg.device_name, cfg.storage_emmc));
    }
    return false;
}

void DeviceNotFoundService::EnsureFound() {
    const auto& cfg = emu_.Get<DeviceConfig>();

#if !CERF_DEV_MODE
    emu_.Get<ConfigurationProblem>().DeviceRomMissing(cfg.device_name);
#endif

    emu_.Get<Fatal>().UserError("device '%s' has no ROM on disk", cfg.device_name.c_str());
}

}
