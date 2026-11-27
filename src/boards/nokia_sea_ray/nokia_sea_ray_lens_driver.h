#pragma once

#include "../../socs/i2c_register_pointer.h"
#include "../../socs/i2c_slave.h"

#include <array>
#include <cstdint>

namespace cerfcore {

class NokiaSeaRayLensDriver : public I2cSlave {
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
    static constexpr uint32_t kRegisterCount = 0x20u;

    uint8_t ReadRegister(uint8_t reg);
    void    WriteRegister(uint8_t reg, uint8_t value);

    I2cRegisterPointer index_;

    std::array<uint8_t, kRegisterCount> registers_{};
    std::array<uint8_t, kRegisterCount> written_{};
};

}
