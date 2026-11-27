#pragma once

#include "../../socs/i2c_register_pointer.h"
#include "../../socs/i2c_slave.h"

#include <cstdint>

namespace cerfcore {

class BatteryPack;
class TiBq27520Monitor;
class TiBq27520RomMode;

class TiBq27520 : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override;
    void OnReady() override;

    bool    MatchesAddress(uint8_t slave_addr) const override;
    bool    OwnsAddress(uint8_t slave_addr) const override;
    void    TxnStart(uint8_t slave_addr) override;
    bool    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte(uint8_t slave_addr) override;
    void    BusStop() override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    uint16_t CommandWord(uint8_t command) const;
    uint16_t ControlWord() const;
    uint16_t FlagsWord() const;
    bool     ItEnabled() const;
    uint16_t BlockDataWord(uint8_t command) const;
    uint32_t RemainingMah() const;
    void     RestartFirmware();
    bool     WriteControl(uint8_t byte);
    bool     ExecuteSubcommand(uint16_t subcommand);

    BatteryPack*      battery_ = nullptr;
    TiBq27520RomMode* rom_     = nullptr;
    TiBq27520Monitor* monitor_ = nullptr;

    bool     rom_txn_             = false;

    I2cRegisterPointer pointer_;
    uint8_t  control_low_         = 0u;
    bool     control_low_written_ = false;
    uint16_t subcommand_          = 0u;
    bool     snooze_              = false;
    bool     hibernate_           = false;
    uint16_t latched_word_        = 0u;
    int32_t  latched_command_     = -1;
    bool     block_access_        = false;
    uint8_t  flash_class_         = 0u;
    uint8_t  flash_block_         = 0u;
};

}
