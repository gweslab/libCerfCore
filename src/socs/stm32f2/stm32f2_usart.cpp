#include "stm32f2_usart.h"

#include "stm32f2_dma.h"
#include "stm32f2_rcc.h"
#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint32_t kSr = 0x00u;
constexpr uint32_t kDr = 0x04u;
constexpr uint32_t kBrr = 0x08u;
constexpr uint32_t kCr1 = 0x0Cu;
constexpr uint32_t kCr2 = 0x10u;
constexpr uint32_t kCr3 = 0x14u;

constexpr uint32_t kOre = 1u << 3;
constexpr uint32_t kIdle = 1u << 4;
constexpr uint32_t kRxne = 1u << 5;
constexpr uint32_t kTc = 1u << 6;
constexpr uint32_t kTxe = 1u << 7;

constexpr uint32_t kRe = 1u << 2;
constexpr uint32_t kTe = 1u << 3;
constexpr uint32_t kUe = 1u << 13;
constexpr uint32_t kOver8 = 1u << 15;
constexpr uint32_t kCr1Unmodelled = 0x1FF3u;
constexpr uint32_t kDmar = 1u << 6;
constexpr uint32_t kDmat = 1u << 7;

constexpr uint64_t kFrameBits = 10u;

}

uint32_t Usart::Read(uint32_t offset, uint32_t size) {
    if (size == 1u && offset != kDr) mcu_.Fault("STM32F2 USART: byte access", base_ + offset);
    switch (offset) {
    case kSr:
        sr_read_ = true;
        return sr_;
    case kDr: {
        const uint32_t value = rdr_;
        sr_ &= ~kRxne;
        if (sr_read_) sr_ &= ~(kOre | kIdle);
        sr_read_ = false;
        UpdateRequests();
        return value;
    }
    case kBrr: return brr_;
    case kCr1: return cr1_;
    case kCr2: return cr2_;
    case kCr3: return cr3_;
    default: mcu_.Fault("STM32F2 USART: read of an unmodelled register", base_ + offset);
    }
    CERF_UNREACHABLE();
}

void Usart::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (size == 1u && offset != kDr) mcu_.Fault("STM32F2 USART: byte access", base_ + offset);
    switch (offset) {
    case kSr:
        if ((value & 0xFFFFFE00u) != 0u) mcu_.Fault("STM32F2 USART: reserved SR bits", value);
        sr_ &= value | ~(kTc | kRxne);
        break;
    case kDr:
        if ((value & 0xFFFFFE00u) != 0u) mcu_.Fault("STM32F2 USART: reserved DR bits", value);
        if ((value & 0x100u) != 0u) mcu_.Fault("STM32F2 USART: 9-bit data is not modelled", value);
        if (sr_read_) sr_ &= ~kTc;
        sr_read_ = false;
        tdr_ = value & 0xFFu;
        sr_ &= ~kTxe;
        if (!shifting_ && (cr1_ & (kUe | kTe)) == (kUe | kTe)) StartShift();
        break;
    case kBrr:
        if ((value & 0xFFFF0000u) != 0u) mcu_.Fault("STM32F2 USART: reserved BRR bits", value);
        if (shifting_ || idle_armed_) mcu_.Fault("STM32F2 USART: a BRR write during a frame is not modelled", value);
        brr_ = value;
        break;
    case kCr1: {
        if ((value & 0xFFFF4000u) != 0u) mcu_.Fault("STM32F2 USART: reserved CR1 bits", value);
        if ((value & ~(kCr1Unmodelled | kRe | kTe | kUe | kOver8)) != 0u)
            mcu_.Fault("STM32F2 USART: reserved CR1 bits", value);
        if ((value & kCr1Unmodelled & ~(kRe | kTe)) != 0u)
            mcu_.Fault("STM32F2 USART: interrupts, parity, wakeup, 9-bit words or break", value);
        if (((cr1_ ^ value) & kOver8) != 0u && (shifting_ || idle_armed_))
            mcu_.Fault("STM32F2 USART: an OVER8 change during a frame is not modelled", value);
        if ((cr1_ & kTe) != 0u && (value & kTe) == 0u && shifting_ && !idle_frame_)
            mcu_.Fault("STM32F2 USART: TE cleared during a frame is not modelled", value);
        const bool te_set = (cr1_ & kTe) == 0u && (value & kTe) != 0u;
        cr1_ = value;
        if (te_set && !shifting_ && (cr1_ & kUe) != 0u) StartIdleFrame();
        if (!shifting_ && (sr_ & kTxe) == 0u && (cr1_ & (kUe | kTe)) == (kUe | kTe)) StartShift();
        break;
    }
    case kCr2:
        if ((value & 0xFFFF8090u) != 0u) mcu_.Fault("STM32F2 USART: reserved CR2 bits", value);
        if (value != 0u) mcu_.Fault("STM32F2 USART: stop bits, clock or LIN configuration", value);
        cr2_ = value;
        break;
    case kCr3:
        if ((value & 0xFFFF000Fu) != 0u) mcu_.Fault("STM32F2 USART: reserved CR3 bits", value);
        if ((value & ~(kDmar | kDmat)) != 0u) mcu_.Fault("STM32F2 USART: CR3 features other than DMA", value);
        cr3_ = value;
        break;
    default: mcu_.Fault("STM32F2 USART: write of an unmodelled register", base_ + offset);
    }
    UpdateRequests();
}

void Usart::ClockChanged() {
    if (shifting_ || idle_armed_) mcu_.Fault("STM32F2 USART: a clock change during a frame is not modelled", base_);
}

uint64_t Usart::BitFckCycles() const {
    const uint64_t oversampling = (cr1_ & kOver8) != 0u ? 8u : 16u;
    if ((cr1_ & kOver8) != 0u && (brr_ & 8u) != 0u) mcu_.Fault("STM32F2 USART: DIV_Fraction[3] set with OVER8", brr_);
    const uint64_t fraction = (cr1_ & kOver8) != 0u ? (brr_ & 7u) : (brr_ & 0xFu);
    const uint64_t bit_fck = oversampling * ((brr_ >> 4) & 0xFFFu) + fraction;
    if (bit_fck == 0u) mcu_.Fault("STM32F2 USART: transmission with a zero baud rate divider", brr_);
    return bit_fck;
}

uint64_t Usart::FrameHclkCycles() const {
    const uint64_t pclk = rcc_.PclkHz(apb2_);
    return (kFrameBits * BitFckCycles() * rcc_.HclkHz() + pclk - 1u) / pclk;
}

void Usart::StartShift() {
    shift_ = tdr_;
    sr_ |= kTxe;
    shifting_ = true;
    idle_frame_ = false;
    shift_remaining_ = FrameHclkCycles();
}

void Usart::StartIdleFrame() {
    shifting_ = true;
    idle_frame_ = true;
    shift_remaining_ = FrameHclkCycles();
}

void Usart::Tick(uint32_t hclk_cycles) {
    if (idle_armed_) {
        if (hclk_cycles >= idle_remaining_) {
            idle_armed_ = false;
            sr_ |= kIdle;
        } else {
            idle_remaining_ -= hclk_cycles;
        }
    }
    uint64_t elapsed = hclk_cycles;
    while (shifting_ && elapsed >= shift_remaining_) {
        elapsed -= shift_remaining_;
        shifting_ = false;
        if (idle_frame_) {
            idle_frame_ = false;
        } else {
            tx_line_->Deliver(static_cast<uint8_t>(shift_));
        }
        if ((sr_ & kTxe) == 0u && (cr1_ & (kUe | kTe)) == (kUe | kTe)) {
            StartShift();
        } else {
            sr_ |= kTc;
        }
        UpdateRequests();
    }
    if (shifting_) shift_remaining_ -= elapsed;
}

bool Usart::TxLevel(bool* level) const {
    if (!shifting_ && (cr1_ & (kUe | kTe)) != (kUe | kTe)) return false;
    if (!shifting_ || idle_frame_) {
        *level = true;
        return true;
    }
    const uint64_t frame = FrameHclkCycles();
    const uint64_t bit = (frame - shift_remaining_) * kFrameBits / frame;
    *level = bit != 0u && (bit > 8u || ((shift_ >> (bit - 1u)) & 1u) != 0u);
    return true;
}

void Usart::Receive(uint8_t value, const Usart& sender) {
    if ((cr1_ & (kUe | kRe)) != (kUe | kRe)) return;
    if (BitFckCycles() * sender.rcc_.PclkHz(sender.apb2_) != sender.BitFckCycles() * rcc_.PclkHz(apb2_))
        mcu_.Fault("STM32F2 USART: a frame at another baud rate than the receiver's", value);
    idle_armed_ = true;
    idle_remaining_ = FrameHclkCycles();
    if ((sr_ & kRxne) != 0u) {
        sr_ |= kOre;
        return;
    }
    rdr_ = value;
    sr_ |= kRxne;
    UpdateRequests();
}

void Usart::UpdateRequests() {
    dma_.SetRequest(rx_stream_, channel_, (cr3_ & kDmar) != 0u && (sr_ & kRxne) != 0u);
    dma_.SetRequest(tx_stream_, channel_, (cr3_ & kDmat) != 0u && (sr_ & kTxe) != 0u);
}

void Usart::SaveState(StateWriter& writer) const {
    writer.Write("usart_sr", sr_);
    writer.Write("usart_brr", brr_);
    writer.Write("usart_cr1", cr1_);
    writer.Write("usart_cr2", cr2_);
    writer.Write("usart_cr3", cr3_);
    writer.Write("usart_tdr", tdr_);
    writer.Write("usart_rdr", rdr_);
    writer.Write("usart_shift", shift_);
    writer.Write("usart_shifting", shifting_);
    writer.Write("usart_idle_frame", idle_frame_);
    writer.Write("usart_sr_read", sr_read_);
    writer.Write("usart_shift_remaining", shift_remaining_);
    writer.Write("usart_idle_armed", idle_armed_);
    writer.Write("usart_idle_remaining", idle_remaining_);
}

void Usart::RestoreState(StateReader& reader) {
    reader.Read("usart_sr", sr_);
    reader.Read("usart_brr", brr_);
    reader.Read("usart_cr1", cr1_);
    reader.Read("usart_cr2", cr2_);
    reader.Read("usart_cr3", cr3_);
    reader.Read("usart_tdr", tdr_);
    reader.Read("usart_rdr", rdr_);
    reader.Read("usart_shift", shift_);
    reader.Read("usart_shifting", shifting_);
    reader.Read("usart_idle_frame", idle_frame_);
    reader.Read("usart_sr_read", sr_read_);
    reader.Read("usart_shift_remaining", shift_remaining_);
    reader.Read("usart_idle_armed", idle_armed_);
    reader.Read("usart_idle_remaining", idle_remaining_);
}

}

}
