#include <cerfcore/core/device_config.h>
#include <cerfcore/core/cerf_emulator.h>
#include "config_loader.h"

namespace cerfcore {

REGISTER_SERVICE(DeviceConfig);

std::string DefaultStorageFileName(std::string_view storage_id) {
    return std::string(storage_id) + ".img";
}

void DeviceConfig::OnReady() {
    emu_.Get<ConfigLoader>().LoadInto(*this);
}

}
