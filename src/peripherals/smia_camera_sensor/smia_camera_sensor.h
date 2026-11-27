#pragma once

#include "../../socs/i2c_slave.h"

#include <array>
#include <cstdint>

namespace cerfcore {

class SmiaCameraSensor : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    void OnReady() override;

    bool    MatchesAddress(uint8_t slave_addr) const override;
    void    TxnStart(uint8_t slave_addr) override;
    bool    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte(uint8_t slave_addr) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void PowerOn();

protected:
    virtual uint8_t  DeviceAddress() const  = 0;
    virtual uint16_t ModelId() const        = 0;
    virtual uint8_t  ManufacturerId() const = 0;

private:
    static constexpr uint32_t kSetupFirst        = 0x0100u;
    static constexpr uint32_t kSetupBytes        = 0x0F00u;
    static constexpr uint32_t kManufacturerFirst = 0x3000u;
    static constexpr uint32_t kManufacturerBytes = 0x1000u;
    static constexpr uint32_t kTransferDataBytes = 64u;

    uint8_t  ReadRegister(uint16_t reg);
    void     WriteRegister(uint16_t reg, uint8_t value);
    void     WriteTransferControl(uint8_t value);
    uint8_t* Storage(uint16_t reg, uint8_t*& written);

    uint16_t index_         = 0u;
    uint16_t pending_index_ = 0u;
    uint8_t  index_bytes_   = 0u;
    bool     index_valid_   = false;

    std::array<uint8_t, kSetupBytes>        setup_{};
    std::array<uint8_t, kSetupBytes>        setup_written_{};
    std::array<uint8_t, kManufacturerBytes> manufacturer_{};
    std::array<uint8_t, kManufacturerBytes> manufacturer_written_{};
    std::array<uint8_t, kTransferDataBytes> transfer_data_{};
    uint8_t                                 transfer_control_ = 0u;
    uint8_t                                 transfer_status_  = 0u;
    uint8_t                                 transfer_page_    = 0u;
};

}
