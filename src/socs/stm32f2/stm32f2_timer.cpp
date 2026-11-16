#include "stm32f2_timer.h"

#include "stm32f2_rcc.h"
#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint32_t kCr1 = 0x00u;
constexpr uint32_t kCr2 = 0x04u;
constexpr uint32_t kSmcr = 0x08u;
constexpr uint32_t kDier = 0x0Cu;
constexpr uint32_t kSr = 0x10u;
constexpr uint32_t kEgr = 0x14u;
constexpr uint32_t kCcmr1 = 0x18u;
constexpr uint32_t kCcmr2 = 0x1Cu;
constexpr uint32_t kCcer = 0x20u;
constexpr uint32_t kCnt = 0x24u;
constexpr uint32_t kPsc = 0x28u;
constexpr uint32_t kArr = 0x2Cu;
constexpr uint32_t kRcr = 0x30u;
constexpr uint32_t kCcr1 = 0x34u;
constexpr uint32_t kCcr4 = 0x40u;
constexpr uint32_t kBdtr = 0x44u;

constexpr uint32_t kCen = 1u << 0;
constexpr uint32_t kUdis = 1u << 1;
constexpr uint32_t kUrs = 1u << 2;
constexpr uint32_t kOpm = 1u << 3;
constexpr uint32_t kArpe = 1u << 7;

constexpr uint32_t kUif = 1u << 0;

}

bool Timer::Reserved(uint32_t offset) const {
    const uint32_t word = offset & ~3u;
    if (Basic()) return word == kSmcr || (word >= kCcmr1 && word <= kCcer) || word >= kRcr;
    return !Advanced() && (word == kRcr || word == kBdtr);
}

[[noreturn]] void Timer::Unmodelled(const char* what, uint32_t detail) {
    mcu_.Fault(what, detail);
    CERF_UNREACHABLE();
}

uint32_t Timer::Read(uint32_t offset, uint32_t size) {
    if (size != 4u && (size != 2u || (offset & 1u) != 0u)) Unmodelled("STM32F2 TIM: byte access", base_ + offset);
    if (Reserved(offset)) Unmodelled("STM32F2 TIM: read of a register this timer does not have", base_ + offset);
    uint32_t value = 0u;
    switch (offset & ~3u) {
    case kCr1: value = state_.cr1; break;
    case kCr2: case kSmcr: value = 0u; break;
    case kDier: value = state_.dier; break;
    case kSr: value = state_.sr; break;
    case kEgr: value = 0u; break;
    case kCcmr1: value = state_.ccmr1; break;
    case kCcmr2: value = state_.ccmr2; break;
    case kCcer: value = state_.ccer; break;
    case kCnt: value = state_.cnt; break;
    case kPsc: value = state_.psc; break;
    case kArr: value = state_.arr; break;
    case kRcr: value = 0u; break;
    case kBdtr: value = state_.bdtr; break;
    default:
        if (offset >= kCcr1 && offset <= kCcr4 + 3u) {
            value = state_.ccr[(offset - kCcr1) / 4u];
            break;
        }
        Unmodelled("STM32F2 TIM: read of an unmodelled register", base_ + offset);
    }
    return value >> ((offset & 2u) * 8u);
}

void Timer::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (size != 4u && (size != 2u || (offset & 3u) != 0u)) Unmodelled("STM32F2 TIM: sub-word write", base_ + offset);
    if (Reserved(offset)) Unmodelled("STM32F2 TIM: write of a register this timer does not have", base_ + offset);
    const uint32_t counter_mask = Wide() ? 0xFFFFFFFFu : 0xFFFFu;
    switch (offset) {
    case kCr1: {
        if ((value & 0xFFFFFC00u) != 0u) Unmodelled("STM32F2 TIM: reserved CR1 bits", value);
        if ((value & 0x70u) != 0u) Unmodelled("STM32F2 TIM: DIR or CMS counting mode", value);
        if ((value & 0x300u) != 0u) Unmodelled("STM32F2 TIM: CKD clock division", value);
        const uint32_t previous = state_.cr1;
        state_.cr1 = value & 0x3FFu;
        if ((previous & kArpe) != 0u && (state_.cr1 & kArpe) == 0u) state_.arr_shadow = state_.arr;
        break;
    }
    case kCr2: case kSmcr: case kRcr:
        if (value != 0u) Unmodelled("STM32F2 TIM: CR2, SMCR or RCR configuration", base_ + offset);
        break;
    case kDier:
        if ((value & ~0x1Fu) != 0u) Unmodelled("STM32F2 TIM: DMA or trigger interrupt enables", value);
        if (Basic() && (value & ~1u) != 0u) Unmodelled("STM32F2 TIM: TIM6/TIM7 DIER bit other than UIE", value);
        if (value != 0u && irq_ == kNoIrq) Unmodelled("STM32F2 TIM: interrupt of a timer without a modelled IRQ", base_);
        state_.dier = value & 0x1Fu;
        break;
    case kSr:
        state_.sr &= value;
        break;
    case kEgr:
        if ((value & ~0x1Fu) != 0u) Unmodelled("STM32F2 TIM: COM, trigger or break generation", value);
        if (Basic() && (value & ~1u) != 0u) Unmodelled("STM32F2 TIM: TIM6/TIM7 EGR bit other than UG", value);
        if ((value & 1u) != 0u) {
            state_.cnt = 0u;
            state_.prescale_count = 0u;
            if ((state_.cr1 & kUdis) == 0u) {
                state_.psc_shadow = state_.psc;
                state_.arr_shadow = state_.arr;
            }
            if ((state_.cr1 & (kUrs | kUdis)) == 0u) state_.sr |= kUif;
        }
        state_.sr |= value & 0x1Eu;
        break;
    case kCcmr1: case kCcmr2: {
        if ((value & 0xFFFF0000u) != 0u) Unmodelled("STM32F2 TIM: reserved CCMR bits", value);
        if ((value & 0x0303u) != 0u) Unmodelled("STM32F2 TIM: input capture", value);
        if ((value & 0x8888u) != 0u) Unmodelled("STM32F2 TIM: output compare preload or clear enable", value);
        uint32_t& ccmr = offset == kCcmr1 ? state_.ccmr1 : state_.ccmr2;
        const uint32_t frozen = LockLevel() == 3u ? 0x7878u : 0u;
        ccmr = (ccmr & frozen) | (value & 0xFFFFu & ~frozen);
        break;
    }
    case kCcer: {
        if ((value & 0xFFFF0000u) != 0u) Unmodelled("STM32F2 TIM: reserved CCER bits", value);
        if ((value & 0xCCCCu) != 0u) Unmodelled("STM32F2 TIM: complementary output enable or polarity", value);
        const uint32_t frozen = LockLevel() >= 2u ? 0xAAAAu : 0u;
        state_.ccer = (state_.ccer & frozen) | (value & 0xFFFFu & ~frozen);
        break;
    }
    case kCnt:
        if ((value & ~counter_mask) != 0u) Unmodelled("STM32F2 TIM: reserved CNT bits", value);
        state_.cnt = value & counter_mask;
        break;
    case kPsc:
        if ((value & 0xFFFF0000u) != 0u) Unmodelled("STM32F2 TIM: reserved PSC bits", value);
        state_.psc = value & 0xFFFFu;
        break;
    case kArr:
        if ((value & ~counter_mask) != 0u) Unmodelled("STM32F2 TIM: reserved ARR bits", value);
        state_.arr = value & counter_mask;
        if ((state_.cr1 & kArpe) == 0u) state_.arr_shadow = state_.arr;
        break;
    case kBdtr: {
        if ((value & 0xFFFF0000u) != 0u) Unmodelled("STM32F2 TIM: reserved BDTR bits", value);
        if ((value & 0x7CFFu) != 0u) Unmodelled("STM32F2 TIM: dead time, off-state, break or automatic output enable", value);
        uint32_t frozen = state_.bdtr_written != 0u ? 0x0300u : 0u;
        if (LockLevel() >= 1u) frozen |= 0x70FFu;
        if (LockLevel() >= 2u) frozen |= 0x0C00u;
        state_.bdtr = (state_.bdtr & frozen) | (value & 0xFFFFu & ~frozen);
        state_.bdtr_written = 1u;
        break;
    }
    default:
        if (offset >= kCcr1 && offset <= kCcr4) {
            if ((value & ~counter_mask) != 0u) Unmodelled("STM32F2 TIM: reserved CCR bits", value);
            state_.ccr[(offset - kCcr1) / 4u] = value & counter_mask;
            break;
        }
        Unmodelled("STM32F2 TIM: write of an unmodelled register", base_ + offset);
    }
    ApplyLevelModes(state_.cnt);
    UpdateIrq();
}

void Timer::ClockChanged() {
    const uint64_t hclk = rcc_.HclkHz();
    if (state_.remainder_hz != 0u) state_.clock_remainder = state_.clock_remainder * hclk / state_.remainder_hz;
    state_.remainder_hz = hclk;
}

void Timer::Tick(uint32_t hclk_cycles) {
    if ((state_.cr1 & kCen) == 0u || state_.arr_shadow == 0u) return;
    const uint64_t hclk = rcc_.HclkHz();
    const uint64_t timer_hz = rcc_.TimerHz(apb2_);
    uint64_t ticks = hclk_cycles;
    if (timer_hz != hclk) {
        state_.remainder_hz = hclk;
        state_.clock_remainder += static_cast<uint64_t>(hclk_cycles) * timer_hz;
        ticks = state_.clock_remainder / hclk;
        state_.clock_remainder %= hclk;
    }
    const uint64_t total = state_.prescale_count + ticks;
    if (state_.psc_shadow == 0u) {
        state_.prescale_count = 0u;
        Count(total);
    } else if (total <= 0xFFFFFFFFu) {
        const uint32_t divisor = state_.psc_shadow + 1u;
        state_.prescale_count = static_cast<uint32_t>(total) % divisor;
        Count(static_cast<uint32_t>(total) / divisor);
    } else {
        const uint64_t divisor = static_cast<uint64_t>(state_.psc_shadow) + 1u;
        state_.prescale_count = static_cast<uint32_t>(total % divisor);
        Count(total / divisor);
    }
    UpdateIrq();
}

uint64_t Timer::CyclesToEvent() const {
    if ((state_.cr1 & kCen) == 0u || state_.arr_shadow == 0u) return UINT64_MAX;
    const bool irq = irq_ != kNoIrq && (state_.dier & 0x1Fu) != 0u;
    uint32_t outputs = 0u;
    for (uint32_t ch = 0u; ch < 4u; ++ch) {
        if (((state_.ccer >> (4u * ch)) & 1u) != 0u || (irq && ((state_.dier >> (ch + 1u)) & 1u) != 0u))
            outputs |= 1u << ch;
    }
    if (!irq && outputs == 0u) return UINT64_MAX;
    if (state_.cnt > state_.arr_shadow) return 1u;
    uint64_t counts = static_cast<uint64_t>(state_.arr_shadow) - state_.cnt + 1u;
    for (uint32_t ch = 0u; ch < 4u; ++ch) {
        if (((outputs >> ch) & 1u) != 0u && state_.ccr[ch] > state_.cnt && state_.ccr[ch] <= state_.arr_shadow)
            counts = std::min<uint64_t>(counts, state_.ccr[ch] - state_.cnt);
    }
    const uint64_t divisor = static_cast<uint64_t>(state_.psc_shadow) + 1u;
    const uint64_t ticks = std::min<uint64_t>(counts * divisor - state_.prescale_count, 0xFFFFFFFFu);
    const uint64_t hclk = rcc_.HclkHz();
    const uint64_t timer_hz = rcc_.TimerHz(apb2_);
    if (timer_hz == hclk) return std::max<uint64_t>(ticks, 1u);
    const uint64_t needed = ticks * hclk - std::min(ticks * hclk, state_.clock_remainder);
    return std::max<uint64_t>((needed + timer_hz - 1u) / timer_hz, 1u);
}

void Timer::Count(uint64_t counts) {
    while (counts != 0u && (state_.cr1 & kCen) != 0u && state_.arr_shadow != 0u) {
        if (state_.cnt > state_.arr_shadow) Unmodelled("STM32F2 TIM: counting from CNT above the auto-reload value", state_.cnt);
        const uint64_t to_overflow = static_cast<uint64_t>(state_.arr_shadow) - state_.cnt + 1u;
        if (counts < to_overflow) {
            Compare(state_.cnt + 1u, static_cast<uint32_t>(state_.cnt + counts));
            state_.cnt = static_cast<uint32_t>(state_.cnt + counts);
            return;
        }
        if (state_.cnt < state_.arr_shadow) Compare(state_.cnt + 1u, state_.arr_shadow);
        counts -= to_overflow;
        state_.cnt = 0u;
        UpdateEvent();
        Compare(0u, 0u);
    }
}

void Timer::UpdateEvent() {
    if ((state_.cr1 & kUdis) != 0u) return;
    state_.sr |= kUif;
    state_.psc_shadow = state_.psc;
    state_.arr_shadow = state_.arr;
    if ((state_.cr1 & kOpm) != 0u) state_.cr1 &= ~kCen;
}

void Timer::Compare(uint32_t first, uint32_t last) {
    if (Basic()) return;
    for (uint32_t ch = 0u; ch < 4u; ++ch) {
        const uint32_t ccmr = ch < 2u ? state_.ccmr1 : state_.ccmr2;
        const uint32_t mode = (ccmr >> (4u + 8u * (ch & 1u))) & 7u;
        const uint32_t ccr = state_.ccr[ch];
        const bool match = ccr >= first && ccr <= last;
        const uint32_t bit = 1u << ch;
        if (!match) continue;
        state_.sr |= 2u << ch;
        if (mode == 1u) state_.oc_ref |= bit;
        if (mode == 2u) state_.oc_ref &= ~bit;
        if (mode == 3u) state_.oc_ref ^= bit;
    }
    ApplyLevelModes(last);
}

void Timer::ApplyLevelModes(uint32_t cnt) {
    for (uint32_t ch = 0u; ch < 4u; ++ch) {
        const uint32_t mode = ((ch < 2u ? state_.ccmr1 : state_.ccmr2) >> (4u + 8u * (ch & 1u))) & 7u;
        const uint32_t bit = 1u << ch;
        if (mode < 4u) continue;
        const bool high = mode == 5u || (mode == 6u && cnt < state_.ccr[ch]) || (mode == 7u && cnt >= state_.ccr[ch]);
        state_.oc_ref = high ? (state_.oc_ref | bit) : (state_.oc_ref & ~bit);
    }
}

void Timer::UpdateIrq() {
    if (irq_ == kNoIrq) return;
    mcu_.SetIrqLevel(irq_, (state_.sr & state_.dier & 0x1Fu) != 0u);
}

void Timer::SaveState(StateWriter& writer) const {
    writer.Write("tim_cr1", state_.cr1);
    writer.Write("tim_dier", state_.dier);
    writer.Write("tim_sr", state_.sr);
    writer.Write("tim_ccmr1", state_.ccmr1);
    writer.Write("tim_ccmr2", state_.ccmr2);
    writer.Write("tim_ccer", state_.ccer);
    writer.Write("tim_cnt", state_.cnt);
    writer.Write("tim_psc", state_.psc);
    writer.Write("tim_arr", state_.arr);
    writer.Write("tim_bdtr", state_.bdtr);
    writer.Write("tim_bdtr_written", state_.bdtr_written);
    writer.Write("tim_ccr", state_.ccr);
    writer.Write("tim_psc_shadow", state_.psc_shadow);
    writer.Write("tim_arr_shadow", state_.arr_shadow);
    writer.Write("tim_prescale_count", state_.prescale_count);
    writer.Write("tim_oc_ref", state_.oc_ref);
    writer.Write("tim_clock_remainder", state_.clock_remainder);
    writer.Write("tim_remainder_hz", state_.remainder_hz);
}

void Timer::RestoreState(StateReader& reader) {
    reader.Read("tim_cr1", state_.cr1);
    reader.Read("tim_dier", state_.dier);
    reader.Read("tim_sr", state_.sr);
    reader.Read("tim_ccmr1", state_.ccmr1);
    reader.Read("tim_ccmr2", state_.ccmr2);
    reader.Read("tim_ccer", state_.ccer);
    reader.Read("tim_cnt", state_.cnt);
    reader.Read("tim_psc", state_.psc);
    reader.Read("tim_arr", state_.arr);
    reader.Read("tim_bdtr", state_.bdtr);
    reader.Read("tim_bdtr_written", state_.bdtr_written);
    reader.Read("tim_ccr", state_.ccr);
    reader.Read("tim_psc_shadow", state_.psc_shadow);
    reader.Read("tim_arr_shadow", state_.arr_shadow);
    reader.Read("tim_prescale_count", state_.prescale_count);
    reader.Read("tim_oc_ref", state_.oc_ref);
    reader.Read("tim_clock_remainder", state_.clock_remainder);
    reader.Read("tim_remainder_hz", state_.remainder_hz);
}

}

}
