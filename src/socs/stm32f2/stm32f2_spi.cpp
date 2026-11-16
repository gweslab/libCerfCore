#include "stm32f2_spi.h"

#include "stm32f2_dma.h"
#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint32_t kCr1 = 0x00u;
constexpr uint32_t kCr2 = 0x04u;
constexpr uint32_t kSr = 0x08u;
constexpr uint32_t kDr = 0x0Cu;
constexpr uint32_t kCrcpr = 0x10u;
constexpr uint32_t kI2scfgr = 0x1Cu;

constexpr uint32_t kRxdmaen = 1u << 0;
constexpr uint32_t kTxdmaen = 1u << 1;
constexpr uint32_t kCr1Modelled = 0x0178u;
constexpr uint32_t kI2scfgrReserved = 0xF040u;
constexpr uint32_t kI2scfgrFields = 0x0FBFu;
constexpr uint32_t kRxne = 1u << 0;
constexpr uint32_t kTxe = 1u << 1;
constexpr uint32_t kOvr = 1u << 6;

}

bool Spi::Peek(uint32_t offset, uint32_t* value) const {
    switch (offset) {
    case kCr1: *value = cr1_; return true;
    case kCr2: *value = cr2_; return true;
    case kI2scfgr: *value = i2scfgr_; return true;
    default: return false;
    }
}

uint32_t Spi::Read(uint32_t offset, uint32_t size) {
    if (size == 1u && offset != kDr) mcu_.Fault("STM32F2 SPI: byte access", base_ + offset);
    switch (offset) {
    case kCr1: return cr1_;
    case kCr2: return cr2_;
    case kSr:
        if (dr_read_) sr_ &= ~kOvr;
        dr_read_ = false;
        return sr_;
    case kDr: {
        sr_ &= ~kRxne;
        dr_read_ = true;
        UpdateRequests();
        return rx_buffer_;
    }
    case kCrcpr: return 7u;
    case kI2scfgr: return i2scfgr_;
    default: mcu_.Fault("STM32F2 SPI: read of an unmodelled register", base_ + offset);
    }
    CERF_UNREACHABLE();
}

void Spi::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (size == 1u && offset != kDr) mcu_.Fault("STM32F2 SPI: byte access", base_ + offset);
    if ((value & 0xFFFF0000u) != 0u) mcu_.Fault("STM32F2 SPI: reserved register bits are set", value);
    switch (offset) {
    case kCr1:
        if ((value & ~kCr1Modelled) != 0u) mcu_.Fault("STM32F2 SPI: CR1 setting other than the modelled slave mode", value);
        cr1_ = value;
        break;
    case kCr2:
        if ((value & ~(kRxdmaen | kTxdmaen)) != 0u) mcu_.Fault("STM32F2 SPI: CR2 setting other than DMA requests", value);
        cr2_ = value;
        break;
    case kSr:
        break;
    case kDr:
        tx_buffer_ = static_cast<uint8_t>(value);
        sr_ &= ~kTxe;
        break;
    case kI2scfgr:
        if ((value & kI2scfgrReserved) != 0u) mcu_.Fault("STM32F2 SPI: I2SCFGR reserved bits are set", value);
        if ((value & kI2scfgrFields) != 0u) mcu_.Fault("STM32F2 SPI: I2S configuration is not modelled", value);
        i2scfgr_ = value;
        break;
    default: mcu_.Fault("STM32F2 SPI: write of an unmodelled register", base_ + offset);
    }
    UpdateRequests();
}

uint8_t Spi::SlaveExchange(uint8_t mosi) {
    if ((sr_ & kTxe) != 0u) mcu_.Fault("STM32F2 SPI: slave frame clocked with an empty transmit buffer", mosi);
    const uint8_t miso = tx_buffer_;
    sr_ |= kTxe;
    if ((sr_ & kRxne) != 0u) {
        sr_ |= kOvr;
    } else {
        rx_buffer_ = mosi;
        sr_ |= kRxne;
    }
    UpdateRequests();
    return miso;
}

void Spi::UpdateRequests() {
    dma_.SetRequest(rx_stream_, channel_, (cr2_ & kRxdmaen) != 0u && (sr_ & kRxne) != 0u);
    dma_.SetRequest(tx_stream_, channel_, (cr2_ & kTxdmaen) != 0u && (sr_ & kTxe) != 0u);
}

void Spi::SaveState(StateWriter& writer) const {
    writer.Write("spi_cr1", cr1_);
    writer.Write("spi_cr2", cr2_);
    writer.Write("spi_sr", sr_);
    writer.Write("spi_i2scfgr", i2scfgr_);
    writer.Write("spi_tx_buffer", tx_buffer_);
    writer.Write("spi_rx_buffer", rx_buffer_);
    writer.Write("spi_dr_read", dr_read_);
}

void Spi::RestoreState(StateReader& reader) {
    reader.Read("spi_cr1", cr1_);
    reader.Read("spi_cr2", cr2_);
    reader.Read("spi_sr", sr_);
    reader.Read("spi_i2scfgr", i2scfgr_);
    reader.Read("spi_tx_buffer", tx_buffer_);
    reader.Read("spi_rx_buffer", rx_buffer_);
    reader.Read("spi_dr_read", dr_read_);
}

}

}
