#include "atmel_mxt224e_nvm.h"

#include "../../core/board_parts.h"
#include "atmel_mxt224e_factory_nvm.h"
#include "atmel_mxt224e_part_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/host_file_bytes.h>
#include <cerfcore/core/log.h>
#include <cerfcore/platform/host_file.h>

#include <algorithm>
#include <string_view>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kCrcPoly     = 0x80001Bu;
constexpr uint32_t kCrcCarry    = 0x1000000u;
constexpr uint32_t kCrcMask     = 0xFFFFFFu;
constexpr size_t   kCrcFromNvm  = kMxtCrcStart - kMxtNvmStart;

uint32_t Crc24Step(uint32_t crc, uint16_t pair) {
    uint32_t result = (crc << 1u) ^ pair;
    if ((result & kCrcCarry) != 0u) result ^= kCrcPoly;
    return result;
}

}

bool AtmelMxt224eNvm::ShouldRegister() { return emu_.Get<BoardParts>().Has(PartId::AtmelMxt224e); }

void AtmelMxt224eNvm::OnReady() {
    const DeviceConfig& cfg = emu_.Get<DeviceConfig>();
    path_ = ResolveDeviceFile(cfg.device_name, cfg.storage_touch_controller);
    if (!HostFileNonEmpty(path_)) {
        bytes_ = emu_.Get<AtmelMxt224eFactoryNvm>().Bytes();
        LOG(Boot, "touch controller: storage.touch_controller %s is absent, the NVM holds the factory configuration\n",
            path_.c_str());
    } else {
        std::vector<uint8_t> file;
        if (!TryReadHostFileBytes(path_, file)) {
            emu_.Get<Fatal>().UserError("storage.touch_controller %s cannot be read", path_.c_str());
        }
        if (file.size() != kMxtNvmBytes) {
            emu_.Get<Fatal>().UserError("storage.touch_controller %s is %zu bytes, not %u",
                                        path_.c_str(), file.size(), kMxtNvmBytes);
        }
        std::copy(file.begin(), file.end(), bytes_.begin());
        LOG(Boot, "touch controller: using storage.touch_controller %s\n", path_.c_str());
    }
    checksum_ = Crc24(std::span<const uint8_t>(bytes_).subspan(kCrcFromNvm));
}

void AtmelMxt224eNvm::Backup(std::span<const uint8_t, kMxtNvmBytes> config) {
    std::copy(config.begin(), config.end(), bytes_.begin());
    checksum_ = Crc24(std::span<const uint8_t>(bytes_).subspan(kCrcFromNvm));
    Persist();
}

uint32_t AtmelMxt224eNvm::Crc24(std::span<const uint8_t> data) {
    uint32_t crc = 0u;
    size_t   i   = 0u;
    for (; i + 1u < data.size(); i += 2u) crc = Crc24Step(crc, cerf::le::U16(data.data() + i));
    if (i < data.size()) crc = Crc24Step(crc, data[i]);
    return crc & kCrcMask;
}

void AtmelMxt224eNvm::Persist() const {
    uint32_t               error = 0;
    const std::string_view view(reinterpret_cast<const char*>(bytes_.data()), bytes_.size());
    if (!host_platform::ReplaceHostFileContents(path_, view, true, error)) {
        emu_.Get<Fatal>().UserError("storage.touch_controller %s cannot be written (error %u)",
                                    path_.c_str(), error);
    }
}

REGISTER_SERVICE(AtmelMxt224eNvm);

}
