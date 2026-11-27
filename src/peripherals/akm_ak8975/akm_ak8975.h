#pragma once

#include "../../socs/i2c_register_pointer.h"
#include "../../socs/i2c_slave.h"
#include <cerfcore/core/callback_list.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class AkmAk8975 : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    void OnReady() override;

    bool    MatchesAddress(uint8_t slave_addr) const override;
    bool    OwnsAddress(uint8_t slave_addr) const override;
    void    TxnStart(uint8_t slave_addr) override;
    bool    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte(uint8_t slave_addr) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void AddDrdyListener(CallbackList<bool>::Callback listener);
    void PowerOn();

protected:
    virtual uint8_t DeviceAddress() const = 0;

private:
    static constexpr uint32_t kDataBytes = 6u;

    void    WriteRegister(uint8_t reg, uint8_t value);
    uint8_t ReadRegister(uint8_t reg);
    void    NextAddress();
    void    WriteControl(uint8_t value);
    void    Measure(int16_t x, int16_t y, int16_t z);
    void    DriveDrdy();

    CallbackList<bool>               drdy_listeners_;
    std::array<uint8_t, kDataBytes>  data_{};
    uint8_t                          mode_            = 0u;
    uint8_t                          astc_            = 0u;
    uint8_t                          st1_             = 0u;
    I2cRegisterPointer               pointer_;
    bool                             i2c_disabled_    = false;
};

}
