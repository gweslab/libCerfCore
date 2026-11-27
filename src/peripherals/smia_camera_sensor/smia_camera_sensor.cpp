#include "smia_camera_sensor.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint16_t kRegModelIdHigh    = 0x0000u;
constexpr uint16_t kRegModelIdLow     = 0x0001u;
constexpr uint16_t kRegManufacturerId = 0x0003u;
constexpr uint16_t kRegSoftwareReset  = 0x0103u;

constexpr uint16_t kRegTransferControl  = 0x0A00u;
constexpr uint16_t kRegTransferStatus   = 0x0A01u;
constexpr uint16_t kRegTransferPage     = 0x0A02u;
constexpr uint16_t kRegTransferReserved = 0x0A03u;
constexpr uint16_t kRegTransferData     = 0x0A04u;
constexpr uint16_t kRegTransfer2First   = 0x0A44u;
constexpr uint16_t kRegTransfer2End     = 0x0A88u;

constexpr uint8_t kTransferEnable     = 1u << 0;
constexpr uint8_t kTransferWrite      = 1u << 1;
constexpr uint8_t kTransferErrorClear = 1u << 2;
constexpr uint8_t kTransferReadReady  = 1u << 0;
constexpr uint8_t kTransferWriteReady = 1u << 1;
constexpr uint8_t kNvmFill            = 0x00u;

constexpr uint8_t kIndexBytes = 2u;

}

void SmiaCameraSensor::OnReady() { PowerOn(); }

void SmiaCameraSensor::PowerOn() {
    index_         = 0u;
    pending_index_ = 0u;
    index_bytes_   = 0u;
    index_valid_   = false;
    setup_.fill(0u);
    setup_written_.fill(0u);
    manufacturer_.fill(0u);
    manufacturer_written_.fill(0u);
    transfer_data_.fill(0u);
    transfer_control_ = 0u;
    transfer_status_  = 0u;
    transfer_page_    = 0u;
}

bool SmiaCameraSensor::MatchesAddress(uint8_t slave_addr) const {
    return slave_addr == DeviceAddress();
}

void SmiaCameraSensor::TxnStart(uint8_t) {
    if (index_bytes_ != 0u && index_bytes_ != kIndexBytes) {
        emu_.Get<Fatal>().Die("SMIA camera sensor: transfer started after %u of %u index bytes",
                              index_bytes_, kIndexBytes);
    }
    index_bytes_ = 0u;
}

bool SmiaCameraSensor::TxnWriteByte(uint8_t, uint8_t byte) {
    if (index_bytes_ < kIndexBytes) {
        pending_index_ = static_cast<uint16_t>((pending_index_ << 8) | byte);
        if (++index_bytes_ == kIndexBytes) {
            index_       = pending_index_;
            index_valid_ = true;
        }
        return true;
    }
    WriteRegister(index_++, byte);
    return true;
}

uint8_t SmiaCameraSensor::TxnReadByte(uint8_t) {
    if (!index_valid_ || (index_bytes_ != 0u && index_bytes_ != kIndexBytes)) {
        emu_.Get<Fatal>().Die("SMIA camera sensor: register read with no index written");
    }
    return ReadRegister(index_++);
}

uint8_t* SmiaCameraSensor::Storage(uint16_t reg, uint8_t*& written) {
    if (reg >= kSetupFirst && reg < kSetupFirst + kSetupBytes) {
        written = &setup_written_[reg - kSetupFirst];
        return &setup_[reg - kSetupFirst];
    }
    if (reg >= kManufacturerFirst && reg < kManufacturerFirst + kManufacturerBytes) {
        written = &manufacturer_written_[reg - kManufacturerFirst];
        return &manufacturer_[reg - kManufacturerFirst];
    }
    written = nullptr;
    return nullptr;
}

uint8_t SmiaCameraSensor::ReadRegister(uint16_t reg) {
    switch (reg) {
    case kRegModelIdHigh:     return static_cast<uint8_t>(ModelId() >> 8);
    case kRegModelIdLow:      return static_cast<uint8_t>(ModelId());
    case kRegManufacturerId:  return ManufacturerId();
    case kRegTransferControl: return transfer_control_;
    case kRegTransferStatus:  return transfer_status_;
    case kRegTransferPage:    return transfer_page_;
    }
    if (reg >= kRegTransferData && reg < kRegTransferData + kTransferDataBytes) {
        return transfer_data_[reg - kRegTransferData];
    }
    const bool special = reg == kRegSoftwareReset || reg == kRegTransferReserved ||
                         (reg >= kRegTransfer2First && reg < kRegTransfer2End);
    uint8_t* written = nullptr;
    uint8_t* value   = special ? nullptr : Storage(reg, written);
    if (value != nullptr && *written != 0u) return *value;
    emu_.Get<Fatal>().Die("SMIA camera sensor: read of register 0x%04X is not modeled", reg);
}

void SmiaCameraSensor::WriteRegister(uint16_t reg, uint8_t value) {
    switch (reg) {
    case kRegTransferControl: WriteTransferControl(value); return;
    case kRegTransferPage:    transfer_page_ = value; return;
    }
    if (reg >= kRegTransferData && reg < kRegTransferData + kTransferDataBytes) {
        transfer_data_[reg - kRegTransferData] = value;
        return;
    }
    const bool special = reg == kRegSoftwareReset || reg == kRegTransferStatus ||
                         reg == kRegTransferReserved ||
                         (reg >= kRegTransfer2First && reg < kRegTransfer2End);
    uint8_t* written = nullptr;
    uint8_t* slot    = special ? nullptr : Storage(reg, written);
    if (slot == nullptr) {
        emu_.Get<Fatal>().Die("SMIA camera sensor: write of 0x%02X to register 0x%04X is not "
                              "modeled", value, reg);
    }
    *slot    = value;
    *written = 1u;
}

void SmiaCameraSensor::WriteTransferControl(uint8_t value) {
    if ((value & ~(kTransferEnable | kTransferWrite | kTransferErrorClear)) != 0u) {
        emu_.Get<Fatal>().Die("SMIA camera sensor: data transfer control 0x%02X", value);
    }
    transfer_control_ = value;
    if ((value & kTransferEnable) == 0u) {
        transfer_status_ = 0u;
        return;
    }
    if ((value & kTransferWrite) != 0u) {
        for (uint8_t byte : transfer_data_) {
            if (byte != kNvmFill) {
                emu_.Get<Fatal>().Die("SMIA camera sensor: NVM page %u write of data the page "
                                      "does not hold", transfer_page_);
            }
        }
        transfer_status_ = kTransferWriteReady;
        return;
    }
    transfer_data_.fill(kNvmFill);
    transfer_status_ = kTransferReadReady;
}

void SmiaCameraSensor::SaveState(StateWriter& w) {
    w.Write<uint16_t>("smia_index", index_);
    w.Write<uint16_t>("smia_pending_index", pending_index_);
    w.Write<uint8_t>("smia_index_bytes", index_bytes_);
    w.Write<uint8_t>("smia_index_valid", index_valid_ ? 1u : 0u);
    w.Write("smia_setup", setup_);
    w.Write("smia_setup_written", setup_written_);
    w.Write("smia_manufacturer", manufacturer_);
    w.Write("smia_manufacturer_written", manufacturer_written_);
    w.Write("smia_transfer_data", transfer_data_);
    w.Write<uint8_t>("smia_transfer_control", transfer_control_);
    w.Write<uint8_t>("smia_transfer_status", transfer_status_);
    w.Write<uint8_t>("smia_transfer_page", transfer_page_);
}

void SmiaCameraSensor::RestoreState(StateReader& r) {
    uint8_t valid = 0u;
    r.Read("smia_index", index_);
    r.Read("smia_pending_index", pending_index_);
    r.Read("smia_index_bytes", index_bytes_);
    r.Read("smia_index_valid", valid);
    r.Read("smia_setup", setup_);
    r.Read("smia_setup_written", setup_written_);
    r.Read("smia_manufacturer", manufacturer_);
    r.Read("smia_manufacturer_written", manufacturer_written_);
    r.Read("smia_transfer_data", transfer_data_);
    r.Read("smia_transfer_control", transfer_control_);
    r.Read("smia_transfer_status", transfer_status_);
    r.Read("smia_transfer_page", transfer_page_);
    index_valid_ = valid != 0u;
}

}
