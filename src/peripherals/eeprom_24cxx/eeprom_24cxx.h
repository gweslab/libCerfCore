#pragma once

#include "../../socs/i2c_register_pointer.h"
#include "../../socs/i2c_slave.h"

#include <cerfcore/platform/host_file.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cerfcore {

class Eeprom24cxx : public I2cSlave {
public:
    static constexpr size_t kBlockBytes = 256u;

    using I2cSlave::I2cSlave;

    void    OnReady() final;
    bool    MatchesAddress(uint8_t slave_addr) const final;
    void    TxnStart(uint8_t slave_addr) final;
    bool    TxnWriteByte(uint8_t slave_addr, uint8_t byte) final;
    uint8_t TxnReadByte(uint8_t slave_addr) final;
    void    BusStart() final;
    void    BusStop() final;
    bool    ResetsWithLine(uint8_t, ResetLineKind kind) const final { return kind == ResetLineKind::Rtc; }

    void SaveState(StateWriter& w) final;
    void RestoreState(StateReader& r) final;

protected:
    virtual void        FillCells() = 0;
    virtual uint8_t     DeviceAddress() const = 0;
    virtual uint16_t    PageBytes() const = 0;
    virtual const char* PartName() const = 0;
    virtual std::string StoragePath() const { return {}; }

    std::array<uint8_t, kBlockBytes>& Cells() { return cells_; }

private:
    static constexpr uint16_t kMaxPageBytes = 16u;

    [[noreturn]] void Refuse(const char* what) const;
    void              OpenStorage(const std::string& path);
    void              StoreCells(uint16_t first, uint16_t count);

    host_platform::HostFile            file_;
    std::string                        path_;
    std::array<uint8_t, kBlockBytes>   cells_{};
    std::array<uint8_t, kMaxPageBytes> page_{};
    uint16_t           page_dirty_ = 0;
    I2cRegisterPointer addr_;
};

}
