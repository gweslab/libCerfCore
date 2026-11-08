#pragma once

#include "../../socs/i2c_slave.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cerfcore {

class Eeprom24cxx : public I2cSlave {
public:
    static constexpr size_t kBlockBytes = 256u;

    using I2cSlave::I2cSlave;

    void    OnReady() final;
    bool    MatchesAddress(uint8_t slave_addr) const final;
    void    TxnStart(uint8_t slave_addr) final;
    void    TxnWriteByte(uint8_t slave_addr, uint8_t byte) final;
    uint8_t TxnReadByte(uint8_t slave_addr) final;
    void    BusStart() final;
    void    BusStop() final;

    void SaveState(StateWriter& w) final;
    void RestoreState(StateReader& r) final;

protected:
    virtual void        FillCells() = 0;
    virtual uint8_t     DeviceAddress() const = 0;
    virtual uint16_t    PageBytes() const = 0;
    virtual const char* PartName() const = 0;

    std::array<uint8_t, kBlockBytes>& Cells() { return cells_; }

private:
    static constexpr uint16_t kMaxPageBytes = 16u;

    [[noreturn]] void Refuse(const char* what) const;

    std::array<uint8_t, kBlockBytes>   cells_{};
    std::array<uint8_t, kMaxPageBytes> page_{};
    uint16_t page_dirty_   = 0;
    uint16_t addr_         = 0;
    bool     addr_valid_   = false;
    bool     addr_pending_ = false;
};

}
