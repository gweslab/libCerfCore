#include "ti_bq27520_flash.h"

#include "../../core/board_parts.h"
#include "ti_bq27520_part_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/host_file_bytes.h>
#include <cerfcore/core/log.h>
#include <cerfcore/platform/host_file.h>

#include <algorithm>
#include <string_view>

namespace cerfcore {

namespace {

constexpr uint8_t kManufacturerInfoDefault = 0x00u;
constexpr uint8_t kItEnableDefault         = 0x00u;

struct FieldDefault {
    uint32_t offset;
    uint8_t  value;
};

constexpr FieldDefault kFieldDefaults[] = {
    {kBq27520RegistersBase + 0u, 0x09u},  {kBq27520RegistersBase + 1u, 0x73u},
    {kBq27520RegistersBase + 7u, 1u},     {kBq27520RegistersBase + 8u, 4u},
    {kBq27520RegistersBase + 9u, 0x00u},  {kBq27520RegistersBase + 10u, 0x00u},
    {kBq27520RegistersBase + 11u, 0x4Bu}, {kBq27520RegistersBase + 12u, 0x2Cu},
    {kBq27520RegistersBase + 13u, 0x3Cu}, {kBq27520RegistersBase + 14u, 0x09u},
    {kBq27520DischargeBase + 5u, 0x0Cu},  {kBq27520DischargeBase + 6u, 0x4Eu},
    {kBq27520DischargeBase + 8u, 0x0Du},  {kBq27520DischargeBase + 9u, 0x48u},
};

constexpr uint32_t kInstructionRowZero = 0u;
constexpr uint32_t kInstructionRowOne  = 1u;

constexpr std::array<uint8_t, kBq27520SignatureBytes> kFirmwareSignature = {0xB2u, 0x6Bu, 0xCFu, 0xABu};

constexpr size_t  kFileStateOff      = kBq27520DataFlashBytes;
constexpr size_t  kFileFlagsOff      = 2u * kBq27520DataFlashBytes;
constexpr size_t  kFileBytes         = kFileFlagsOff + 1u;
constexpr uint8_t kFlagRowZeroErased = 1u;
constexpr uint8_t kLastByteState     = 2u;

}

bool TiBq27520Flash::ShouldRegister() {
    return emu_.Get<BoardParts>().Has(PartId::TiBq27520);
}

void TiBq27520Flash::OnReady() {
    const DeviceConfig& cfg = emu_.Get<DeviceConfig>();
    path_ = ResolveDeviceFile(cfg.device_name, cfg.storage_fuel_gauge);
    if (!HostFileNonEmpty(path_)) {
        state_.fill(ByteState::kUnknown);
        for (uint32_t i = 0; i < kBq27520ManufacturerInfoSize; ++i) {
            data_[kBq27520ManufacturerInfoBase + i]  = kManufacturerInfoDefault;
            state_[kBq27520ManufacturerInfoBase + i] = ByteState::kKnown;
        }
        data_[kBq27520ItEnableOffset]  = kItEnableDefault;
        state_[kBq27520ItEnableOffset] = ByteState::kKnown;
        for (const FieldDefault& field : kFieldDefaults) {
            data_[field.offset]  = field.value;
            state_[field.offset] = ByteState::kKnown;
        }
        LOG(Boot, "fuel gauge: storage.fuel_gauge %s is absent, the data flash holds its defaults\n",
            path_.c_str());
        return;
    }
    std::vector<uint8_t> bytes;
    if (!TryReadHostFileBytes(path_, bytes)) {
        emu_.Get<Fatal>().UserError("storage.fuel_gauge %s cannot be read", path_.c_str());
    }
    Load(bytes);
    LOG(Boot, "fuel gauge: using storage.fuel_gauge %s\n", path_.c_str());
}

void TiBq27520Flash::Load(const std::vector<uint8_t>& bytes) {
    if (bytes.size() != kFileBytes) {
        emu_.Get<Fatal>().UserError("storage.fuel_gauge %s is %zu bytes, not %zu", path_.c_str(),
                                    bytes.size(), kFileBytes);
    }
    for (size_t i = 0; i < kBq27520DataFlashBytes; ++i) {
        const uint8_t state = bytes[kFileStateOff + i];
        if (state > kLastByteState) {
            emu_.Get<Fatal>().UserError("storage.fuel_gauge %s: data flash byte 0x%03zX has state %u",
                                        path_.c_str(), i, state);
        }
        data_[i]  = bytes[i];
        state_[i] = static_cast<ByteState>(state);
    }
    const uint8_t flags = bytes[kFileFlagsOff];
    if ((flags & ~kFlagRowZeroErased) != 0u) {
        emu_.Get<Fatal>().UserError("storage.fuel_gauge %s: flags byte 0x%02X", path_.c_str(), flags);
    }
    instruction_row0_erased_ = flags != 0u;
}

void TiBq27520Flash::Persist() const {
    std::vector<uint8_t> bytes(kFileBytes, 0u);
    std::copy(data_.begin(), data_.end(), bytes.begin());
    for (size_t i = 0; i < kBq27520DataFlashBytes; ++i) {
        bytes[kFileStateOff + i] = static_cast<uint8_t>(state_[i]);
    }
    bytes[kFileFlagsOff] = instruction_row0_erased_ ? kFlagRowZeroErased : 0u;
    uint32_t error = 0;
    const std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!host_platform::ReplaceHostFileContents(path_, view, true, error)) {
        emu_.Get<Fatal>().UserError("storage.fuel_gauge %s cannot be written (error %u)", path_.c_str(),
                                    error);
    }
}

uint8_t TiBq27520Flash::ReadDataFlash(uint32_t offset) const {
    if (offset >= kBq27520DataFlashBytes || state_[offset] != ByteState::kKnown) {
        emu_.Get<Fatal>().Die("ti bq27520: data flash byte 0x%03X is read in state %u, and its "
                              "content is not modeled", offset,
                              offset < kBq27520DataFlashBytes ? static_cast<unsigned>(state_[offset]) : 0u);
    }
    return data_[offset];
}

void TiBq27520Flash::WriteDataFlashByte(uint32_t offset, uint8_t value) {
    data_[offset]  = value;
    state_[offset] = ByteState::kKnown;
    Persist();
}

uint16_t TiBq27520Flash::DataFlashSum() const {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < kBq27520DataFlashBytes; ++i) sum += ReadDataFlash(i);
    return static_cast<uint16_t>(sum);
}

void TiBq27520Flash::EraseDataFlash() {
    data_.fill(0u);
    state_.fill(ByteState::kErased);
    Persist();
}

void TiBq27520Flash::ProgramDataFlashRow(uint32_t row,
                                         std::span<const uint8_t, kBq27520DataFlashRowBytes> data) {
    if (row >= kBq27520DataFlashRows) {
        emu_.Get<Fatal>().Die("ti bq27520: the host programmed data flash row %u of %u", row,
                              kBq27520DataFlashRows);
    }
    const uint32_t base = row * kBq27520DataFlashRowBytes;
    for (uint32_t i = 0; i < kBq27520DataFlashRowBytes; ++i) {
        if (state_[base + i] != ByteState::kErased) {
            emu_.Get<Fatal>().Die("ti bq27520: the host programmed data flash row %u over byte 0x%03X "
                                  "in state %u, and programming over unerased flash is not modeled",
                                  row, base + i, static_cast<unsigned>(state_[base + i]));
        }
        data_[base + i]  = data[i];
        state_[base + i] = ByteState::kKnown;
    }
    Persist();
}

void TiBq27520Flash::EraseInstructionRow(uint32_t row) {
    if (row != kInstructionRowZero) {
        emu_.Get<Fatal>().Die("ti bq27520: the host erased instruction flash row %u, and only row %u "
                              "is modeled", row, kInstructionRowZero);
    }
    instruction_row0_erased_ = true;
    Persist();
}

void TiBq27520Flash::ProgramInstructionRow(uint32_t row) {
    if (row == kInstructionRowOne) return;
    if (row != kInstructionRowZero) {
        emu_.Get<Fatal>().Die("ti bq27520: the host programmed instruction flash row %u, and only rows "
                              "%u and %u are modeled", row, kInstructionRowZero, kInstructionRowOne);
    }
    if (!instruction_row0_erased_) {
        emu_.Get<Fatal>().Die("ti bq27520: the host programmed instruction flash row %u without erasing "
                              "it", row);
    }
    instruction_row0_erased_ = false;
    Persist();
}

std::array<uint8_t, kBq27520SignatureBytes> TiBq27520Flash::FirmwareSignature() const {
    if (instruction_row0_erased_) {
        emu_.Get<Fatal>().Die("ti bq27520: the host read the firmware signature with instruction flash "
                              "row %u erased", kInstructionRowZero);
    }
    return kFirmwareSignature;
}

REGISTER_SERVICE(TiBq27520Flash);

}
