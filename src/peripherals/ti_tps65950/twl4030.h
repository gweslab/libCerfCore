#pragma once

#include "../../socs/i2c_register_pointer.h"
#include "../../socs/i2c_slave.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace cerfcore {

struct Tps65950RegValue;

class Twl4030 : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override;
    void OnReady() override;

    bool MatchesAddress(uint8_t slave_addr) const override;

    void    TxnStart    (uint8_t slave_addr) override;
    bool    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte (uint8_t slave_addr) override;

    uint8_t AudioReg(uint8_t sub_addr) const;
    uint8_t PowerReg(uint8_t sub_addr) const;
    void    SetPowerReg(uint8_t sub_addr, uint8_t value);
    void    LoadPowerOn(bool backup_domain);
    void    ResetDigital();

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    int AddrIndex(uint8_t slave_addr) const;
    void       LoadBankLocked(uint8_t slave_addr, const Tps65950RegValue* image, size_t count);
    void       LoadDigitalLocked();
    void       LoadPowerLocked(bool backup_domain);
    void       AudioWillChange(bool timing, bool uplink);
    void       AudioChanged(bool timing, bool uplink, bool downlink);
    bool       ApplyMsecure(uint8_t sub, uint8_t& byte);
    uint8_t    AdvanceSubLocked(int idx);

    static constexpr int kAddrCount = 4;
    mutable std::mutex state_mutex_;
    std::array<std::array<uint8_t, 256>, kAddrCount> regs_{};
    std::array<I2cRegisterPointer, kAddrCount>       sub_addr_{};
    bool                                             audio_reset_ = false;
};

}
