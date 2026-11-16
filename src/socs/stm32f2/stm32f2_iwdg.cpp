#include "stm32f2_iwdg.h"

#include "stm32f2_rcc.h"
#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint64_t kLsiHz = 32000u;
}

uint32_t Iwdg::Read(uint32_t offset, uint32_t size) {
    if (size == 1u) mcu_.Fault("STM32F2 IWDG: byte access", kBase + offset);
    switch (offset) {
    case 0x00u: return 0u;
    case 0x04u: return pr_;
    case 0x08u: return rlr_;
    case 0x0Cu: return 0u;
    default: mcu_.Fault("STM32F2 IWDG: read of an unmodelled register", kBase + offset);
    }
    CERF_UNREACHABLE();
}

void Iwdg::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (size == 1u) mcu_.Fault("STM32F2 IWDG: byte access", kBase + offset);
    switch (offset) {
    case 0x00u:
        if ((value & 0xFFFF0000u) != 0u) mcu_.Fault("STM32F2 IWDG: reserved KR bits are set", value);
        unlocked_ = value == 0x5555u;
        if (value == 0xCCCCu && !running_) {
            running_ = true;
            counter_ = 0x0FFFu;
        } else if (value == 0xAAAAu) {
            counter_ = rlr_;
        }
        UpdateDeadline();
        return;
    case 0x04u:
        if ((value & ~7u) != 0u) mcu_.Fault("STM32F2 IWDG: reserved PR bits are set", value);
        if (unlocked_) pr_ = value & 7u;
        UpdateDeadline();
        return;
    case 0x08u:
        if ((value & ~0x0FFFu) != 0u) mcu_.Fault("STM32F2 IWDG: reserved RLR bits are set", value);
        if (unlocked_) rlr_ = value & 0x0FFFu;
        return;
    default: mcu_.Fault("STM32F2 IWDG: write of an unmodelled register", kBase + offset);
    }
}

void Iwdg::Tick(uint32_t hclk_cycles) {
    const uint64_t hclk = rcc_.HclkHz();
    if (!running_ || expired_) {
        if (running_) UpdateDeadline();
        return;
    }
    remainder_hz_ = hclk;
    lsi_remainder_ += static_cast<uint64_t>(hclk_cycles) * kLsiHz;
    if (lsi_remainder_ < hclk) return;
    const uint64_t lsi_ticks = lsi_remainder_ / hclk;
    lsi_remainder_ %= hclk;
    const uint64_t divider = Divider();
    const uint64_t counts = (prescaler_ + lsi_ticks) / divider - prescaler_ / divider;
    prescaler_ = static_cast<uint32_t>((prescaler_ + lsi_ticks) % kPrescalerStates);
    if (counts > counter_) {
        counter_ = 0u;
        expired_ = true;
        mcu_.RequestSystemReset(Rcc::kIwdgResetFlag);
    } else {
        counter_ -= static_cast<uint32_t>(counts);
    }
    UpdateDeadline();
}

uint64_t Iwdg::Divider() const {
    return pr_ >= 6u ? 256u : (4u << pr_);
}

void Iwdg::ClockChanged() {
    const uint64_t hclk = rcc_.HclkHz();
    if (remainder_hz_ != 0u) {
        lsi_remainder_ = lsi_remainder_ * hclk / remainder_hz_;
    }
    remainder_hz_ = hclk;
    UpdateDeadline();
}

void Iwdg::UpdateDeadline() {
    if (!running_ || expired_) {
        mcu_.SetResetDeadline(UINT64_MAX);
        return;
    }
    const uint64_t divider = Divider();
    const uint64_t lsi_ticks = (prescaler_ / divider + counter_ + 1u) * divider - prescaler_;
    const uint64_t needed = lsi_ticks * rcc_.HclkHz() - lsi_remainder_;
    mcu_.SetResetDeadline(mcu_.Now() + (needed + kLsiHz - 1u) / kLsiHz);
}

void Iwdg::SaveState(StateWriter& writer) const {
    writer.Write("iwdg_pr", pr_);
    writer.Write("iwdg_rlr", rlr_);
    writer.Write("iwdg_counter", counter_);
    writer.Write("iwdg_lsi_remainder", lsi_remainder_);
    writer.Write("iwdg_remainder_hz", remainder_hz_);
    writer.Write("iwdg_prescaler", prescaler_);
    writer.Write("iwdg_running", running_);
    writer.Write("iwdg_unlocked", unlocked_);
    writer.Write("iwdg_expired", expired_);
}

void Iwdg::RestoreState(StateReader& reader) {
    reader.Read("iwdg_pr", pr_);
    reader.Read("iwdg_rlr", rlr_);
    reader.Read("iwdg_counter", counter_);
    reader.Read("iwdg_lsi_remainder", lsi_remainder_);
    reader.Read("iwdg_remainder_hz", remainder_hz_);
    reader.Read("iwdg_prescaler", prescaler_);
    reader.Read("iwdg_running", running_);
    reader.Read("iwdg_unlocked", unlocked_);
    reader.Read("iwdg_expired", expired_);
}

}

}
