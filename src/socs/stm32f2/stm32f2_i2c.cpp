#include "stm32f2_i2c.h"
#include "stm32f2_rcc.h"

#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint32_t kCr1 = 0x00u;
constexpr uint32_t kCr2 = 0x04u;
constexpr uint32_t kOar1 = 0x08u;
constexpr uint32_t kOar2 = 0x0Cu;
constexpr uint32_t kDr = 0x10u;
constexpr uint32_t kSr1 = 0x14u;
constexpr uint32_t kSr2 = 0x18u;
constexpr uint32_t kCcr = 0x1Cu;
constexpr uint32_t kTrise = 0x20u;

constexpr uint32_t kPe = 1u << 0;
constexpr uint32_t kStart = 1u << 8;
constexpr uint32_t kStop = 1u << 9;
constexpr uint32_t kAck = 1u << 10;
constexpr uint32_t kSwrst = 1u << 15;
constexpr uint32_t kReservedCr1 = (1u << 14) | (1u << 2);

constexpr uint32_t kSb = 1u << 0;
constexpr uint32_t kAddr = 1u << 1;
constexpr uint32_t kBtf = 1u << 2;
constexpr uint32_t kRxne = 1u << 6;
constexpr uint32_t kTxe = 1u << 7;
constexpr uint32_t kAf = 1u << 10;
constexpr uint32_t kMsl = 1u << 0;
constexpr uint32_t kBusy = 1u << 1;
constexpr uint32_t kTra = 1u << 2;
constexpr uint32_t kFreqMask = 0x3Fu;
constexpr uint32_t kFastMode = 1u << 15;
constexpr uint32_t kDuty = 1u << 14;

}

void I2c::SetHeldInReset(bool held) {
    if (held) {
        Release();
        cr1_ = 0u;
        cr2_ = 0u;
        oar1_ = 0u;
        ccr_ = 0u;
        trise_ = 2u;
        sr1_ = 0u;
        sr2_ = 0u;
        dr_ = 0u;
        sr1_read_with_addr_ = false;
    }
    held_in_reset_ = held;
}

uint32_t I2c::Read(uint32_t offset, uint32_t size) {
    if (held_in_reset_) mcu_.Fault("STM32F2 I2C: access while held in reset is not modelled", base_ + offset);
    if (size == 1u) mcu_.Fault("STM32F2 I2C: byte access", base_ + offset);
    switch (offset) {
    case kCr1: return cr1_;
    case kCr2: return cr2_;
    case kOar1: return oar1_;
    case kOar2: return 0u;
    case kCcr: return ccr_;
    case kTrise: return trise_;
    case kSr1:
        sr1_read_with_addr_ = (sr1_ & kAddr) != 0u;
        return sr1_;
    case kSr2: {
        const uint32_t value = sr2_;
        if (sr1_read_with_addr_ && (sr1_ & kAddr) != 0u) {
            sr1_ &= ~kAddr;
            if ((sr2_ & kTra) != 0u) {
                sr1_ |= kTxe;
            } else {
                dr_ = device_->ReadByte();
                sr1_ |= kRxne;
            }
        }
        sr1_read_with_addr_ = false;
        return value;
    }
    case kDr: {
        const uint32_t value = dr_;
        sr1_ &= ~(kRxne | kBtf);
        if (addressed_ && (sr2_ & kTra) == 0u && (cr1_ & kAck) != 0u) {
            dr_ = device_->ReadByte();
            sr1_ |= kRxne;
        }
        return value;
    }
    default: mcu_.Fault("STM32F2 I2C: read of an unmodelled register", base_ + offset);
    }
    CERF_UNREACHABLE();
}

void I2c::Release() {
    EndTransfer();
    sr2_ &= ~(kMsl | kBusy | kTra);
    sr1_ &= ~(kSb | kAddr | kBtf | kRxne | kTxe);
}

uint32_t I2c::EndTransfer() {
    const uint32_t cleared = kTxe | ((sr2_ & kTra) != 0u ? kBtf : 0u);
    if (addressed_) device_->Stop();
    addressed_ = false;
    return cleared;
}

uint32_t I2c::Pclk1MHz() {
    const uint64_t pclk = rcc_.PclkHz(false);
    if (pclk % 1000000u != 0u) mcu_.Fault("STM32F2 I2C: PCLK1 is not an integer MHz", static_cast<uint32_t>(pclk));
    return static_cast<uint32_t>(pclk / 1000000u);
}

void I2c::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (held_in_reset_) mcu_.Fault("STM32F2 I2C: access while held in reset is not modelled", base_ + offset);
    if (size == 1u) mcu_.Fault("STM32F2 I2C: byte access", base_ + offset);
    switch (offset) {
    case kCr1:
        if ((value & 0xFFFF0000u) != 0u) mcu_.Fault("STM32F2 I2C: reserved CR1 bits", value);
        if ((value & kSwrst) != 0u) mcu_.Fault("STM32F2 I2C: software reset is not modelled", value);
        if ((value & kReservedCr1) != 0u) mcu_.Fault("STM32F2 I2C: reserved CR1 bits are set", value);
        if ((value & 0x38FAu) != 0u) mcu_.Fault("STM32F2 I2C: SMBus, PEC, POS or target features are not modelled", value);
        cr1_ = value;
        if ((value & kPe) == 0u) {
            Release();
            cr1_ &= ~(kStart | kStop);
            return;
        }
        if ((value & kStart) != 0u) {
            sr1_ &= ~EndTransfer();
            sr2_ |= kMsl | kBusy;
            sr1_ |= kSb;
        }
        if ((value & kStop) != 0u) {
            sr1_ &= ~EndTransfer();
            sr2_ &= ~(kMsl | kBusy | kTra);
        }
        cr1_ &= ~(kStart | kStop);
        return;
    case kCr2:
        if ((value & 0xFFFFE0C0u) != 0u) mcu_.Fault("STM32F2 I2C: reserved CR2 bits", value);
        if ((value & 0x1F00u) != 0u) mcu_.Fault("STM32F2 I2C: interrupts and DMA are not modelled", value);
        {
            const uint32_t freq = value & kFreqMask;
            const uint32_t actual = Pclk1MHz();
            if (freq < 2u || freq > 50u)
                mcu_.Fault("STM32F2 I2C: CR2 FREQ outside RM0033 limits", freq);
            if (freq != actual) mcu_.Fault("STM32F2 I2C: CR2 FREQ does not match PCLK1 MHz", freq);
        }
        cr2_ = value & 0x3Fu;
        return;
    case kOar1: oar1_ = value; return;
    case kCcr: {
        if ((value & 0xFFFF3000u) != 0u) mcu_.Fault("STM32F2 I2C: reserved CCR bits are set", value);
        if ((cr1_ & kPe) != 0u) mcu_.Fault("STM32F2 I2C: CCR write while peripheral enabled", value);
        if ((value & 0x3000u) != 0u) mcu_.Fault("STM32F2 I2C: reserved CCR bits are set", value);
        const uint32_t ccr = value & 0x0FFFu;
        const uint32_t minimum = (value & (kFastMode | kDuty)) == (kFastMode | kDuty) ? 1u : 4u;
        if (ccr < minimum) mcu_.Fault("STM32F2 I2C: CCR below RM0033 minimum", ccr);
        ccr_ = value;
        return;
    }
    case kTrise: {
        if ((value & 0xFFFFFFC0u) != 0u) mcu_.Fault("STM32F2 I2C: reserved TRISE bits are set", value);
        if ((cr1_ & kPe) != 0u) mcu_.Fault("STM32F2 I2C: TRISE write while peripheral enabled", value);
        trise_ = value & 0x3Fu;
        return;
    }
    case kSr1:
        sr1_ &= value | 0x00FFu;
        return;
    case kDr:
        if ((sr1_ & kSb) != 0u) {
            sr1_ &= ~kSb;
            const bool read = (value & 1u) != 0u;
            if (device_->Start((value >> 1) & 0x7Fu, read)) {
                addressed_ = true;
                sr1_ |= kAddr;
                sr2_ = read ? (sr2_ & ~kTra) : (sr2_ | kTra);
            } else {
                sr1_ |= kAf;
            }
            return;
        }
        if (!addressed_ || (sr2_ & kTra) == 0u)
            mcu_.Fault("STM32F2 I2C: DR write outside a controller transmission", value);
        sr1_ &= ~(kTxe | kBtf);
        if (!device_->WriteByte(static_cast<uint8_t>(value))) sr1_ |= kAf;
        sr1_ |= kTxe | kBtf;
        return;
    default: mcu_.Fault("STM32F2 I2C: write of an unmodelled register", base_ + offset);
    }
}

void I2c::SaveState(StateWriter& writer) const {
    writer.Write("i2c_cr1", cr1_);
    writer.Write("i2c_cr2", cr2_);
    writer.Write("i2c_oar1", oar1_);
    writer.Write("i2c_ccr", ccr_);
    writer.Write("i2c_trise", trise_);
    writer.Write("i2c_sr1", sr1_);
    writer.Write("i2c_sr2", sr2_);
    writer.Write("i2c_dr", dr_);
    writer.Write("i2c_sr1_read_with_addr", sr1_read_with_addr_);
    writer.Write("i2c_addressed", addressed_);
    writer.Write("i2c_held_in_reset", held_in_reset_);
}

void I2c::RestoreState(StateReader& reader) {
    reader.Read("i2c_cr1", cr1_);
    reader.Read("i2c_cr2", cr2_);
    reader.Read("i2c_oar1", oar1_);
    reader.Read("i2c_ccr", ccr_);
    reader.Read("i2c_trise", trise_);
    reader.Read("i2c_sr1", sr1_);
    reader.Read("i2c_sr2", sr2_);
    reader.Read("i2c_dr", dr_);
    reader.Read("i2c_sr1_read_with_addr", sr1_read_with_addr_);
    reader.Read("i2c_addressed", addressed_);
    reader.Read("i2c_held_in_reset", held_in_reset_);
}

}

}
