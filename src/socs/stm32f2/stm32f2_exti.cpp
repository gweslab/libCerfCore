#include "stm32f2_exti.h"

#include "stm32f2_gpio.h"
#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint32_t kImr = 0x00u;
constexpr uint32_t kEmr = 0x04u;
constexpr uint32_t kRtsr = 0x08u;
constexpr uint32_t kFtsr = 0x0Cu;
constexpr uint32_t kSwier = 0x10u;
constexpr uint32_t kPr = 0x14u;
constexpr uint32_t kGpioLines = 0xFFFFu;
constexpr uint32_t kLineMask = 0x7FFFFFu;

}

uint32_t Syscfg::Read(uint32_t offset, uint32_t size) {
    if (size != 4u) mcu_.Fault("STM32F2 SYSCFG: sub-word access", kBase + offset);
    if (offset == 0x00u) return memrmp_;
    if (offset >= 0x08u && offset <= 0x14u) return exticr_[(offset - 0x08u) / 4u];
    mcu_.Fault("STM32F2 SYSCFG: read of an unmodelled register", kBase + offset);
    CERF_UNREACHABLE();
}

void Syscfg::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (size != 4u) mcu_.Fault("STM32F2 SYSCFG: sub-word access", kBase + offset);
    if (offset == 0x00u) {
        if ((value & 0xFFFFFFFCu) != 0u) mcu_.Fault("STM32F2 SYSCFG: reserved MEMRMP bits are set", value);
        if ((value & 3u) != 0u) mcu_.Fault("STM32F2 SYSCFG: memory remap is not modelled", value);
        memrmp_ = value & 3u;
        return;
    }
    if (offset >= 0x08u && offset <= 0x14u) {
        if ((value & 0xFFFF0000u) != 0u) mcu_.Fault("STM32F2 SYSCFG: reserved EXTICR bits are set", value);
        exticr_[(offset - 0x08u) / 4u] = value & 0xFFFFu;
        return;
    }
    mcu_.Fault("STM32F2 SYSCFG: write of an unmodelled register", kBase + offset);
}

void Syscfg::SaveState(StateWriter& writer) const {
    writer.Write("syscfg_memrmp", memrmp_);
    writer.Write("syscfg_exticr", exticr_);
}

void Syscfg::RestoreState(StateReader& reader) {
    reader.Read("syscfg_memrmp", memrmp_);
    reader.Read("syscfg_exticr", exticr_);
}

uint32_t Exti::Read(uint32_t offset, uint32_t size) {
    if (size != 4u) mcu_.Fault("STM32F2 EXTI: sub-word access", kBase + offset);
    switch (offset) {
    case kImr: return imr_;
    case kEmr: return emr_;
    case kRtsr: return rtsr_;
    case kFtsr: return ftsr_;
    case kSwier: mcu_.Fault("STM32F2 EXTI: the software interrupt event register is not modelled", kBase + offset);
    case kPr: return pr_;
    default: mcu_.Fault("STM32F2 EXTI: read of an unmodelled register", kBase + offset);
    }
    CERF_UNREACHABLE();
}

void Exti::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (size != 4u) mcu_.Fault("STM32F2 EXTI: sub-word access", kBase + offset);
    if ((value & 0xFF800000u) != 0u) mcu_.Fault("STM32F2 EXTI: reserved line bits are set", value);
    value &= kLineMask;
    if (offset != kPr && (value & ~kGpioLines) != 0u)
        mcu_.Fault("STM32F2 EXTI: lines 16 to 22 are not modelled", value);
    switch (offset) {
    case kImr: imr_ = value; break;
    case kEmr:
        if (value != 0u) mcu_.Fault("STM32F2 EXTI: event mask is not modelled", value);
        emr_ = value;
        break;
    case kRtsr: case kFtsr: {
        Sample();
        uint32_t& trigger = offset == kRtsr ? rtsr_ : ftsr_;
        sampled_ &= rtsr_ | ftsr_;
        pr_ &= ~(trigger ^ value);
        trigger = value;
        break;
    }
    case kSwier: mcu_.Fault("STM32F2 EXTI: the software interrupt event register is not modelled", value);
    case kPr: pr_ &= ~value; break;
    default: mcu_.Fault("STM32F2 EXTI: write of an unmodelled register", kBase + offset);
    }
    UpdateIrq();
}

void Exti::Tick(uint32_t) {
    Sample();
}

void Exti::Sample() {
    const uint32_t watched = rtsr_ | ftsr_;
    for (uint32_t line = 0u; line < 16u; ++line) {
        const uint32_t bit = 1u << line;
        if ((watched & bit) == 0u) continue;
        const uint32_t port = syscfg_.ExtiPort(line);
        if (port >= gpio_.size()) mcu_.Fault("STM32F2 EXTI: reserved port selection", port);
        const bool level = gpio_[port]->PinLevel(line);
        const bool old = (levels_ & bit) != 0u;
        if ((sampled_ & bit) != 0u && level != old) {
            if ((level && (rtsr_ & bit) != 0u) || (!level && (ftsr_ & bit) != 0u)) pr_ |= bit;
        }
        sampled_ |= bit;
        levels_ = level ? (levels_ | bit) : (levels_ & ~bit);
    }
    UpdateIrq();
}

void Exti::UpdateIrq() {
    const uint32_t active = pr_ & imr_;
    for (uint32_t line = 0u; line < 5u; ++line) mcu_.SetIrqLevel(6u + line, (active & (1u << line)) != 0u);
    mcu_.SetIrqLevel(23u, (active & 0x03E0u) != 0u);
    mcu_.SetIrqLevel(40u, (active & 0xFC00u) != 0u);
}

void Exti::SaveState(StateWriter& writer) const {
    writer.Write("exti_imr", imr_);
    writer.Write("exti_emr", emr_);
    writer.Write("exti_rtsr", rtsr_);
    writer.Write("exti_ftsr", ftsr_);
    writer.Write("exti_pr", pr_);
    writer.Write("exti_levels", levels_);
    writer.Write("exti_sampled", sampled_);
}

void Exti::RestoreState(StateReader& reader) {
    reader.Read("exti_imr", imr_);
    reader.Read("exti_emr", emr_);
    reader.Read("exti_rtsr", rtsr_);
    reader.Read("exti_ftsr", ftsr_);
    reader.Read("exti_pr", pr_);
    reader.Read("exti_levels", levels_);
    reader.Read("exti_sampled", sampled_);
}

}

}
