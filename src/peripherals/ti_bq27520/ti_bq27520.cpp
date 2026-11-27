#include "ti_bq27520.h"

#include "../../core/board_parts.h"
#include "../battery_pack.h"
#include "ti_bq27520_flash.h"
#include "ti_bq27520_monitor.h"
#include "ti_bq27520_part_id.h"
#include "ti_bq27520_rom_mode.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint8_t kAddr        = 0x55u;
constexpr uint8_t kRomAddr     = 0x0Bu;
constexpr uint8_t kLastCommand = 0x6Bu;

constexpr uint8_t kCmdControlLow  = 0x00u;
constexpr uint8_t kCmdControlHigh = 0x01u;
constexpr uint8_t kCmdAtRate      = 0x02u;
constexpr uint8_t kCmdAtRateTte   = 0x04u;
constexpr uint8_t kCmdTemperature = 0x06u;
constexpr uint8_t kCmdVoltage     = 0x08u;
constexpr uint8_t kCmdFlags       = 0x0Au;
constexpr uint8_t kCmdNac         = 0x0Cu;
constexpr uint8_t kCmdFac         = 0x0Eu;
constexpr uint8_t kCmdRm          = 0x10u;
constexpr uint8_t kCmdFcc         = 0x12u;
constexpr uint8_t kCmdAi          = 0x14u;
constexpr uint8_t kCmdTte         = 0x16u;
constexpr uint8_t kCmdUnlisted18  = 0x18u;
constexpr uint8_t kCmdSi          = 0x1Au;
constexpr uint8_t kCmdStte        = 0x1Cu;
constexpr uint8_t kCmdUnlisted1E  = 0x1Eu;
constexpr uint8_t kCmdUnlisted20  = 0x20u;
constexpr uint8_t kCmdAe          = 0x22u;
constexpr uint8_t kCmdAp          = 0x24u;
constexpr uint8_t kCmdTtecp       = 0x26u;
constexpr uint8_t kCmdSoh         = 0x28u;
constexpr uint8_t kCmdCycleCount  = 0x2Au;
constexpr uint8_t kCmdSoc         = 0x2Cu;

constexpr uint8_t kCmdDataFlashClass   = 0x3Eu;
constexpr uint8_t kCmdDataFlashBlock   = 0x3Fu;
constexpr uint8_t kCmdBlockDataFirst   = 0x40u;
constexpr uint8_t kCmdBlockDataLast    = 0x5Fu;
constexpr uint8_t kCmdBlockDataControl = 0x61u;

constexpr uint8_t kBlockDataUnsealedAccess = 0x00u;
constexpr uint8_t kClassManufacturerInfo   = 57u;
constexpr uint8_t kManufacturerInfoBlock   = 0u;

constexpr uint16_t kSubControlStatus  = 0x0000u;
constexpr uint16_t kSubDeviceType     = 0x0001u;
constexpr uint16_t kSubBatInsert      = 0x000Du;
constexpr uint16_t kSubBatRemove      = 0x000Eu;
constexpr uint16_t kSubSetHibernate   = 0x0011u;
constexpr uint16_t kSubClearHibernate = 0x0012u;
constexpr uint16_t kSubSetSleepPlus   = 0x0013u;
constexpr uint16_t kSubClearSleepPlus = 0x0014u;
constexpr uint16_t kSubItEnable       = 0x0021u;
constexpr uint16_t kSubReset          = 0x0041u;
constexpr uint16_t kSubEnterRomMode   = 0x0F00u;

constexpr uint8_t kItDisabled = 0x00u;
constexpr uint8_t kItEnabled  = 0x01u;

constexpr uint16_t kDeviceType = 0x0520u;

constexpr uint16_t kStatusInitComp  = 1u << 7;
constexpr uint16_t kStatusHibernate = 1u << 6;
constexpr uint16_t kStatusSnooze    = 1u << 5;
constexpr uint16_t kStatusVok       = 1u << 1;
constexpr uint16_t kStatusQen       = 1u << 0;

constexpr uint16_t kFlagDsg     = 1u << 0;
constexpr uint16_t kFlagSysDown = 1u << 1;
constexpr uint16_t kFlagSoc1    = 1u << 2;
constexpr uint16_t kFlagBatDet  = 1u << 3;
constexpr uint16_t kFlagChg     = 1u << 8;

constexpr uint16_t kNotApplicable = 0xFFFFu;
constexpr uint16_t kNoCurrent     = 0u;
constexpr uint16_t kNoCycles      = 0u;
constexpr uint16_t kUnlistedWord  = 0u;
constexpr uint16_t kAtRateReset   = 0u;
constexpr uint16_t kStateOfHealth = 100u;

constexpr int      kDeciKelvinAtZeroCelsius = 2731;
constexpr int      kDeciPerUnit             = 10;
constexpr int      kFullPercent             = 100;
constexpr uint32_t kMilliPerUnit            = 1000u;

constexpr int kNoLatch = -1;

}

bool TiBq27520::ShouldRegister() {
    return emu_.Get<BoardParts>().Has(PartId::TiBq27520);
}

void TiBq27520::OnReady() {
    battery_ = &emu_.Get<BatteryPack>();
    rom_     = &emu_.Get<TiBq27520RomMode>();
    monitor_ = &emu_.Get<TiBq27520Monitor>();
}

bool TiBq27520::MatchesAddress(uint8_t slave_addr) const {
    return slave_addr == (rom_->Active() ? kRomAddr : kAddr);
}

bool TiBq27520::OwnsAddress(uint8_t slave_addr) const {
    return slave_addr == kAddr || slave_addr == kRomAddr;
}

void TiBq27520::TxnStart(uint8_t) {
    rom_txn_ = rom_->Active();
    if (rom_txn_) {
        rom_->TxnStart();
        return;
    }
    pointer_.Start();
    latched_command_ = kNoLatch;
}

bool TiBq27520::TxnWriteByte(uint8_t, uint8_t byte) {
    if (rom_txn_) {
        rom_->WriteByte(byte);
        return true;
    }
    if (pointer_.Select(byte)) return byte <= kLastCommand;
    const uint8_t command = static_cast<uint8_t>(pointer_.Register());
    bool restart = false;
    switch (command) {
    case kCmdControlLow:
    case kCmdControlHigh:
        restart = WriteControl(byte);
        break;
    case kCmdBlockDataControl:
        if (byte != kBlockDataUnsealedAccess) {
            emu_.Get<Fatal>().Die(
                "ti bq27520: the host wrote 0x%02X to BlockDataControl(), and only the "
                "unsealed block access 0x%02X is modeled", byte, kBlockDataUnsealedAccess);
        }
        block_access_ = true;
        break;
    case kCmdDataFlashClass:
        flash_class_ = byte;
        break;
    case kCmdDataFlashBlock:
        flash_block_ = byte;
        break;
    default:
        emu_.Get<Fatal>().Die(
            "ti bq27520: the host wrote 0x%02X to command 0x%02X, which is not modeled",
            byte, command);
    }
    pointer_.Advance();
    if (restart) RestartFirmware();
    return true;
}

uint16_t TiBq27520::BlockDataWord(uint8_t command) const {
    if (!block_access_ || flash_class_ != kClassManufacturerInfo ||
        flash_block_ != kManufacturerInfoBlock) {
        emu_.Get<Fatal>().Die(
            "ti bq27520: the host read BlockData() 0x%02X with block access %u, class %u "
            "and block %u, and only the Manufacturer Info block is modeled", command,
            block_access_ ? 1u : 0u, flash_class_, flash_block_);
    }
    const auto&    flash  = emu_.Get<TiBq27520Flash>();
    const uint32_t offset = kBq27520ManufacturerInfoBase + (command - kCmdBlockDataFirst);
    return static_cast<uint16_t>(flash.ReadDataFlash(offset) | (flash.ReadDataFlash(offset + 1u) << 8));
}

uint8_t TiBq27520::TxnReadByte(uint8_t) {
    if (rom_txn_) return rom_->ReadByte();
    if (pointer_.Register() > kLastCommand) {
        emu_.Get<Fatal>().Die("ti bq27520: the host read past command 0x%02X", kLastCommand);
    }
    const uint8_t command = static_cast<uint8_t>(pointer_.Advance());
    const uint8_t even    = static_cast<uint8_t>(command & ~1u);
    if (command == even || latched_command_ != even) {
        latched_word_    = CommandWord(even);
        latched_command_ = even;
    }
    return static_cast<uint8_t>(command == even ? latched_word_ : latched_word_ >> 8);
}

void TiBq27520::BusStop() {
    if (rom_txn_) {
        rom_txn_ = false;
        if (!rom_->Active()) RestartFirmware();
        return;
    }
    if (control_low_written_) {
        emu_.Get<Fatal>().Die(
            "ti bq27520: the host ended a transfer after the low byte 0x%02X of a "
            "Control() subcommand", control_low_);
    }
}

void TiBq27520::RestartFirmware() {
    pointer_.Reset(0u, true);
    control_low_         = 0u;
    control_low_written_ = false;
    subcommand_          = kSubControlStatus;
    snooze_              = false;
    hibernate_           = false;
    latched_word_        = 0u;
    latched_command_     = kNoLatch;
    block_access_        = false;
    flash_class_         = 0u;
    flash_block_         = 0u;
    monitor_->Restart();
}

bool TiBq27520::WriteControl(uint8_t byte) {
    if (pointer_.Register() == kCmdControlLow) {
        control_low_         = byte;
        control_low_written_ = true;
        return false;
    }
    if (!control_low_written_) {
        emu_.Get<Fatal>().Die(
            "ti bq27520: the host wrote the high byte 0x%02X of a Control() subcommand "
            "without its low byte", byte);
    }
    control_low_written_ = false;
    return ExecuteSubcommand(static_cast<uint16_t>(control_low_ | (byte << 8)));
}

bool TiBq27520::ExecuteSubcommand(uint16_t subcommand) {
    LOG(Periph, "ti bq27520: Control() subcommand 0x%04X\n", subcommand);
    switch (subcommand) {
    case kSubControlStatus:
    case kSubDeviceType:
        break;
    case kSubBatInsert: monitor_->InsertBattery(); break;
    case kSubBatRemove: monitor_->RemoveBattery(); break;
    case kSubSetHibernate:   hibernate_ = true;  break;
    case kSubClearHibernate: hibernate_ = false; break;
    case kSubSetSleepPlus:   snooze_    = true;  break;
    case kSubClearSleepPlus: snooze_    = false; break;
    case kSubEnterRomMode:   rom_->Enter();     break;
    case kSubItEnable:
        monitor_->IndicateDataFlashWrite();
        emu_.Get<TiBq27520Flash>().WriteDataFlashByte(kBq27520ItEnableOffset, kItEnabled);
        break;
    case kSubReset:
        return true;
    default:
        emu_.Get<Fatal>().Die("ti bq27520: Control() subcommand 0x%04X is not modeled",
                              subcommand);
    }
    subcommand_ = subcommand;
    return false;
}

uint16_t TiBq27520::ControlWord() const {
    switch (subcommand_) {
    case kSubControlStatus:
        return static_cast<uint16_t>(kStatusInitComp | (hibernate_ ? kStatusHibernate : 0u) |
                                     (snooze_ ? kStatusSnooze : 0u) |
                                     (ItEnabled() ? kStatusVok | kStatusQen : 0u));
    case kSubDeviceType:
        return kDeviceType;
    }
    emu_.Get<Fatal>().Die("ti bq27520: the host read Control() after subcommand 0x%04X",
                          subcommand_);
}

bool TiBq27520::ItEnabled() const {
    const uint8_t it_enable = emu_.Get<TiBq27520Flash>().ReadDataFlash(kBq27520ItEnableOffset);
    if (it_enable != kItDisabled && it_enable != kItEnabled) {
        emu_.Get<Fatal>().Die("ti bq27520: IT Enable 0x%02X is not modeled", it_enable);
    }
    return it_enable == kItEnabled;
}

uint16_t TiBq27520::FlagsWord() const {
    uint16_t flags = 0u;
    if (monitor_->BatteryDetected()) flags |= kFlagBatDet;
    if (monitor_->Soc1()) flags |= kFlagSoc1;
    if (monitor_->SysDown()) flags |= kFlagSysDown;
    if (!battery_->Charging()) flags |= kFlagDsg;
    if (battery_->ChargePercent() < kFullPercent) flags |= kFlagChg;
    return flags;
}

uint32_t TiBq27520::RemainingMah() const {
    return battery_->DesignCapacityMah() * static_cast<uint32_t>(battery_->ChargePercent()) /
           static_cast<uint32_t>(kFullPercent);
}

uint16_t TiBq27520::CommandWord(uint8_t command) const {
    switch (command) {
    case kCmdControlLow:  return ControlWord();
    case kCmdAtRate:      return kAtRateReset;
    case kCmdAtRateTte:   return kNotApplicable;
    case kCmdTemperature:
        return static_cast<uint16_t>(kDeciKelvinAtZeroCelsius +
                                     kDeciPerUnit * battery_->TemperatureCelsius());
    case kCmdVoltage:     return static_cast<uint16_t>(battery_->VoltageMv());
    case kCmdFlags:       return FlagsWord();
    case kCmdNac:
    case kCmdRm:          return static_cast<uint16_t>(RemainingMah());
    case kCmdFac:
    case kCmdFcc:         return static_cast<uint16_t>(battery_->DesignCapacityMah());
    case kCmdAi:          return kNoCurrent;
    case kCmdTte:         return kNotApplicable;
    case kCmdUnlisted18:
    case kCmdUnlisted1E:
    case kCmdUnlisted20:  return kUnlistedWord;
    case kCmdSi:          return kNoCurrent;
    case kCmdStte:        return kNotApplicable;
    case kCmdAe:
        return static_cast<uint16_t>(RemainingMah() * battery_->VoltageMv() / kMilliPerUnit);
    case kCmdAp:          return kNoCurrent;
    case kCmdTtecp:       return kNotApplicable;
    case kCmdSoh:         return kStateOfHealth;
    case kCmdCycleCount:  return kNoCycles;
    case kCmdSoc:         return static_cast<uint16_t>(battery_->ChargePercent());
    }
    if (command >= kCmdBlockDataFirst && command <= kCmdBlockDataLast) {
        return BlockDataWord(command);
    }
    emu_.Get<Fatal>().Die("ti bq27520: the host read command 0x%02X, which is not modeled",
                          command);
}

void TiBq27520::SaveState(StateWriter& w) {
    w.Write<uint8_t>("bq27520_pointer", static_cast<uint8_t>(pointer_.Register()));
    w.Write<uint8_t>("bq27520_command_pending", pointer_.Pending() ? 1u : 0u);
    w.Write<uint8_t>("bq27520_control_low", control_low_);
    w.Write<uint8_t>("bq27520_control_low_written", control_low_written_ ? 1u : 0u);
    w.Write<uint16_t>("bq27520_subcommand", subcommand_);
    w.Write<uint8_t>("bq27520_snooze", snooze_ ? 1u : 0u);
    w.Write<uint8_t>("bq27520_hibernate", hibernate_ ? 1u : 0u);
    w.Write<uint16_t>("bq27520_latched_word", latched_word_);
    w.Write<int32_t>("bq27520_latched_command", latched_command_);
    w.Write<uint8_t>("bq27520_block_access", block_access_ ? 1u : 0u);
    w.Write<uint8_t>("bq27520_flash_class", flash_class_);
    w.Write<uint8_t>("bq27520_flash_block", flash_block_);
    w.Write<uint8_t>("bq27520_rom_txn", rom_txn_ ? 1u : 0u);
    rom_->SaveState(w);
    monitor_->SaveState(w);
}

void TiBq27520::RestoreState(StateReader& r) {
    uint8_t pointer = 0u, pending = 0u, written = 0u, snooze = 0u, hibernate = 0u;
    r.Read("bq27520_pointer", pointer);
    r.Read("bq27520_command_pending", pending);
    r.Read("bq27520_control_low", control_low_);
    r.Read("bq27520_control_low_written", written);
    r.Read("bq27520_subcommand", subcommand_);
    r.Read("bq27520_snooze", snooze);
    r.Read("bq27520_hibernate", hibernate);
    r.Read("bq27520_latched_word", latched_word_);
    r.Read("bq27520_latched_command", latched_command_);
    uint8_t block_access = 0u;
    r.Read("bq27520_block_access", block_access);
    r.Read("bq27520_flash_class", flash_class_);
    r.Read("bq27520_flash_block", flash_block_);
    uint8_t rom_txn = 0u;
    r.Read("bq27520_rom_txn", rom_txn);
    rom_->RestoreState(r);
    monitor_->RestoreState(r);
    rom_txn_             = rom_txn != 0u;
    block_access_        = block_access != 0u;
    pointer_.Restore(pointer, true, pending != 0u);
    control_low_written_ = written != 0u;
    snooze_              = snooze != 0u;
    hibernate_           = hibernate != 0u;
}

REGISTER_SERVICE(TiBq27520);

}
