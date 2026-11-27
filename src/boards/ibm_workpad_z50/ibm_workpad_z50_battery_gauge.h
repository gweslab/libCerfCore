#pragma once

#include "../../socs/i2c_register_pointer.h"
#include "../../socs/i2c_slave.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace cerfcore {

class GuestCycleClock;
class IbmWorkpadZ50Battery;

class IbmWorkpadZ50BatteryGauge : public I2cSlave {
public:
    static constexpr uint8_t kAddress = 0x0Bu;

    using I2cSlave::I2cSlave;

    bool ShouldRegister() override;
    void OnReady() override;

    bool    MatchesAddress(uint8_t slave_addr) const override;
    void    TxnStart(uint8_t slave_addr) override;
    bool    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte(uint8_t slave_addr) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    static constexpr size_t kReadBytes = 6u;

    uint8_t  Command() const;
    uint16_t Word(uint8_t cmd) const;
    void     LatchRead();
    void     WriteWord(uint8_t cmd, uint16_t value);
    uint16_t RemainingMah() const;
    uint16_t BatteryStatus() const;
    uint16_t Flags() const;
    bool     Unlocked() const;
    void     TrackValidDischarge();

    GuestCycleClock*      clock_   = nullptr;
    IbmWorkpadZ50Battery* battery_ = nullptr;

    uint16_t rca_          = 0;
    uint16_t rta_          = 0;
    uint16_t fcc_          = 0;
    uint16_t staged_fcc_   = 0;
    bool     fcc_staged_   = false;
    int64_t  busy_until_ns_ = 0;

    I2cRegisterPointer cmd_;
    uint8_t write_lo_    = 0;
    uint8_t write_count_ = 0;
    std::array<uint8_t, kReadBytes> read_{};
    uint8_t read_len_ = 0;
    uint8_t read_idx_ = 0;

    mutable std::mutex vdq_mtx_;
    bool               vdq_      = false;
    int                vdq_fill_ = 0;
};

}
