#pragma once

#include "stm32f2_mcu.h"

#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class Dma;

class Spi : public Peripheral {
public:
    Spi(Mcu& mcu, uint32_t base, Dma& dma, uint32_t rx_stream, uint32_t tx_stream, uint32_t channel)
        : mcu_(mcu), base_(base), dma_(dma), rx_stream_(rx_stream), tx_stream_(tx_stream), channel_(channel) {}

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    bool Peek(uint32_t offset, uint32_t* value) const override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;

    uint8_t SlaveExchange(uint8_t mosi);
    bool Enabled() const { return (cr1_ & (1u << 6)) != 0u; }

private:
    void UpdateRequests();

    Mcu& mcu_;
    uint32_t base_;
    Dma& dma_;
    uint32_t rx_stream_;
    uint32_t tx_stream_;
    uint32_t channel_;
    uint32_t cr1_ = 0u;
    uint32_t cr2_ = 0u;
    uint32_t sr_ = 0x0002u;
    uint32_t i2scfgr_ = 0u;
    uint8_t tx_buffer_ = 0u;
    uint8_t rx_buffer_ = 0u;
    bool dr_read_ = false;
};

}

}
