#pragma once

#include "../../socs/i2c_register_pointer.h"
#include "../../socs/i2c_slave.h"

#include <cstdint>

namespace cerfcore {

class KionixKxtf9 : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override;
    void OnReady() override;

    bool    MatchesAddress(uint8_t slave_addr) const override;
    void    TxnStart(uint8_t slave_addr) override;
    bool    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte(uint8_t slave_addr) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void PowerOn();

private:
    void     WriteRegister(uint8_t reg, uint8_t value);
    uint8_t  ReadRegister(uint8_t reg);
    uint8_t  ReadOutput(uint8_t reg);
    uint16_t Sensitivity() const;

    I2cRegisterPointer pointer_;
    uint8_t            ctrl1_     = 0u;
    uint8_t            int_ctrl1_ = 0u;
    uint8_t            data_ctrl_ = 0u;
};

}
