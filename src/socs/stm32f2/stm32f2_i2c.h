#pragma once

#include "stm32f2_mcu.h"

#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class Rcc;

class I2cDevice {
public:
    virtual ~I2cDevice() = default;
    virtual bool Start(uint32_t address7, bool read) = 0;
    virtual bool WriteByte(uint8_t value) = 0;
    virtual uint8_t ReadByte() = 0;
    virtual void Stop() = 0;
};

class I2c : public Peripheral {
public:
    I2c(Mcu& mcu, Rcc& rcc, uint32_t base, I2cDevice* device)
        : mcu_(mcu), rcc_(rcc), base_(base), device_(device) {}

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;

    void SetHeldInReset(bool held);

private:
    void Release();
    uint32_t EndTransfer();
    uint32_t Pclk1MHz();

    Mcu& mcu_;
    Rcc& rcc_;
    uint32_t base_;
    I2cDevice* device_;
    uint32_t cr1_ = 0u;
    uint32_t cr2_ = 0u;
    uint32_t oar1_ = 0u;
    uint32_t ccr_ = 0u;
    uint32_t trise_ = 2u;
    uint32_t sr1_ = 0u;
    uint32_t sr2_ = 0u;
    uint32_t dr_ = 0u;
    bool sr1_read_with_addr_ = false;
    bool addressed_ = false;
    bool held_in_reset_ = false;
};

}

}
