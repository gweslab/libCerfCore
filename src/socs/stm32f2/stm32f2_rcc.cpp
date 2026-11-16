#include "stm32f2_rcc.h"
#include "stm32f2_register.h"

#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

#include <string>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint64_t kHsiHz = 16000000u;

constexpr uint32_t kSourceReady[3] = {1u << 1, 1u << 17, 1u << 25};

const char* ClockEnableName(uint32_t offset, uint32_t bit) {
    switch (offset) {
    case 0x30u:
        switch (bit) {
        case 0u: return "GPIOA"; case 1u: return "GPIOB"; case 2u: return "GPIOC"; case 3u: return "GPIOD";
        case 4u: return "GPIOE"; case 5u: return "GPIOF"; case 6u: return "GPIOG"; case 7u: return "GPIOH";
        case 8u: return "GPIOI"; case 12u: return "CRC"; case 18u: return "BKPSRAM";
        case 21u: return "DMA1"; case 22u: return "DMA2"; case 25u: return "ETHMAC";
        case 26u: return "ETHMACTX"; case 27u: return "ETHMACRX"; case 28u: return "ETHMACPTP";
        case 29u: return "OTGHS"; case 30u: return "OTGHSULPI";
        default: return nullptr;
        }
    case 0x34u:
        switch (bit) {
        case 0u: return "DCMI"; case 4u: return "CRYP"; case 5u: return "HASH";
        case 6u: return "RNG"; case 7u: return "OTGFS";
        default: return nullptr;
        }
    case 0x40u:
        switch (bit) {
        case 0u: return "TIM2"; case 1u: return "TIM3"; case 2u: return "TIM4"; case 3u: return "TIM5";
        case 4u: return "TIM6"; case 5u: return "TIM7"; case 6u: return "TIM12"; case 7u: return "TIM13";
        case 8u: return "TIM14"; case 11u: return "WWDG"; case 14u: return "SPI2"; case 15u: return "SPI3";
        case 17u: return "USART2"; case 18u: return "USART3"; case 19u: return "UART4"; case 20u: return "UART5";
        case 21u: return "I2C1"; case 22u: return "I2C2"; case 23u: return "I2C3";
        case 25u: return "CAN1"; case 26u: return "CAN2";
        case 28u: return "PWR"; case 29u: return "DAC";
        default: return nullptr;
        }
    case 0x44u:
        switch (bit) {
        case 0u: return "TIM1"; case 1u: return "TIM8"; case 4u: return "USART1"; case 5u: return "USART6";
        case 8u: return "ADC1"; case 9u: return "ADC2"; case 10u: return "ADC3"; case 11u: return "SDIO";
        case 12u: return "SPI1"; case 14u: return "SYSCFG"; case 16u: return "TIM9"; case 17u: return "TIM10";
        case 18u: return "TIM11";
        default: return nullptr;
        }
    default:
        return nullptr;
    }
}

bool ClockEnableModelled(uint32_t offset, uint32_t bit) {
    switch (offset) {
    case 0x30u: return bit <= 8u || bit == 21u || bit == 22u;
    case 0x34u: return bit == 6u;
    case 0x40u: return bit == 0u || bit == 1u || bit == 2u || bit == 4u || bit == 14u || bit == 21u;
    case 0x44u: return bit == 0u || bit == 4u || bit == 14u;
    default: return true;
    }
}

uint32_t ClockReservedMask(uint32_t offset) {
    switch (offset) {
    case 0x30u: return 0x819BEE00u;
    case 0x34u: return 0xFFFFFF0Eu;
    case 0x40u: return 0xC9013600u;
    case 0x44u: return 0xFFF8A0CCu;
    case 0x50u: return 0x81986E00u;
    case 0x54u: return 0xFFFFFF0Eu;
    case 0x60u: return 0xC9013600u;
    case 0x64u: return 0xFFF8A0CCu;
    default: return 0u;
    }
}

const char* ClockRegisterName(uint32_t offset) {
    switch (offset) {
    case 0x30u: return "RCC_AHB1ENR";
    case 0x34u: return "RCC_AHB2ENR";
    case 0x40u: return "RCC_APB1ENR";
    case 0x44u: return "RCC_APB2ENR";
    case 0x50u: return "RCC_AHB1LPENR";
    case 0x54u: return "RCC_AHB2LPENR";
    case 0x60u: return "RCC_APB1LPENR";
    case 0x64u: return "RCC_APB2LPENR";
    default: return "RCC clock register";
    }
}

}

bool Rcc::Modelled(uint32_t offset) const {
    switch (offset & ~3u) {
    case 0x00u: case 0x04u: case 0x08u: case 0x0Cu: case 0x10u: case 0x14u: case 0x18u:
    case 0x20u: case 0x24u: case 0x30u: case 0x34u: case 0x38u: case 0x40u: case 0x44u:
    case 0x50u: case 0x54u: case 0x58u: case 0x60u: case 0x64u: case 0x74u:
        return true;
    default:
        return false;
    }
}

uint32_t Rcc::Read(uint32_t offset, uint32_t) {
    if (!Modelled(offset)) mcu_.Fault("STM32F2 RCC: read of an unmodelled register", kBase + offset);
    return regs_[offset / 4u] >> ((offset & 3u) * 8u);
}

void Rcc::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (!Modelled(offset)) mcu_.Fault("STM32F2 RCC: write of an unmodelled register", kBase + offset);
    const uint32_t word = offset & ~3u;
    const bool clock = word == kCfgr || word == kPllcfgr || word == kCr;
    const bool gate = word == 0x30u || word == 0x34u || word == 0x40u || word == 0x44u;
    if (clock || gate) mcu_.FlushTicks();
    uint32_t v = MergeRegisterWrite(regs_[word / 4u], offset, size, value);
    if ((v & ClockReservedMask(word)) != 0u) {
        const std::string fault = std::string("STM32F2 RCC: reserved bits in ") + ClockRegisterName(word);
        mcu_.Fault(fault.c_str(), v);
    }
    if (gate) {
        const uint32_t newly_enabled = v & ~regs_[word / 4u];
        for (uint32_t bit = 0u; bit < 32u; ++bit) {
            if (((newly_enabled >> bit) & 1u) == 0u || ClockEnableModelled(word, bit)) continue;
            if (const char* name = ClockEnableName(word, bit)) {
                const std::string fault = std::string("STM32F2 RCC: ") + name + " clock enable is not modelled";
                mcu_.Fault(fault.c_str(), v);
            }
        }
    }
    switch (word) {
    case 0x38u:
        if (v != 0u) mcu_.Fault("STM32F2 RCC: FSMC and reserved AHB3ENR bits are not modelled", v);
        break;
    case 0x50u: case 0x54u: case 0x58u: case 0x60u: case 0x64u:
        if (v != regs_[word / 4u]) mcu_.Fault("STM32F2 RCC: low-power mode clock enables are not modelled", v);
        break;
    case kCr:
        if (((v ^ regs_[kCr / 4u]) & 0x000C00F8u) != 0u) mcu_.Fault("STM32F2 RCC: HSITRIM, HSEBYP or CSSON", v);
        if ((regs_[kCr / 4u] & ~v & ClockSourcesInUse((v & (1u << 24)) != 0u)) != 0u)
            mcu_.Fault("STM32F2 RCC: stopping a clock in use", v);
        if ((v & (1u << 26)) != 0u) mcu_.Fault("STM32F2 RCC: the PLLI2S is not modelled", v);
        if ((v & ~regs_[kCr / 4u] & (1u << 24)) != 0u) CheckPllConfiguration();
        v &= 0x050D00F9u;
        v |= (v & 1u) << 1;
        v |= (v & (1u << 16)) << 1;
        v |= (v & (1u << 24)) << 1;
        v |= (v & (1u << 26)) << 1;
        v |= regs_[kCr / 4u] & 0x0000FF00u;
        break;
    case kCfgr:
        if ((v & 0xFFFF0000u) != 0u) mcu_.Fault("STM32F2 RCC: MCO, RTCPRE or I2SSRC are not modelled", v);
        if ((v & 3u) == 3u) mcu_.Fault("STM32F2 RCC: reserved SW value", v);
        if ((regs_[kCr / 4u] & kSourceReady[v & 3u]) == 0u)
            mcu_.Fault("STM32F2 RCC: a switch to a clock source that is not ready is not modelled", v);
        v = (v & ~0xCu) | ((v & 3u) << 2);
        break;
    case kPllcfgr:
        if (size == 1u) mcu_.Fault("STM32F2 RCC: byte write of RCC_PLLCFGR", offset);
        if ((regs_[kCr / 4u] & (1u << 24)) != 0u && v != regs_[kPllcfgr / 4u])
            mcu_.Fault("STM32F2 RCC: PLLCFGR write while the PLL is on", v);
        break;
    case kCir:
        if ((v & 0x00003F00u) != 0u) mcu_.Fault("STM32F2 RCC: clock ready interrupts are not modelled", v);
        v &= 0x00003F00u;
        break;
    case 0x10u: case 0x14u: case 0x18u: case 0x20u: case 0x24u:
        ApplyPeripheralReset(word, regs_[word / 4u], v);
        break;
    case 0x30u: case 0x34u: case 0x40u: case 0x44u:
        if ((regs_[word / 4u] & ~v) != 0u) mcu_.Fault("STM32F2 RCC: stopping a peripheral clock is not modelled", v);
        break;
    case kCsr:
        if ((v & (1u << 24)) != 0u) {
            v &= 0x00000001u;
        } else {
            v = (v & 1u) | ResetFlags();
        }
        v |= (v & 1u) << 1;
        break;
    default:
        break;
    }
    regs_[word / 4u] = v;
    if (gate) mcu_.FlushTicks();
    if (!clock) return;
    Recompute();
    mcu_.ClockChanged();
}

void Rcc::ApplyPeripheralReset(uint32_t word, uint32_t old_value, uint32_t value) {
    uint32_t changed = old_value ^ value;
    for (const auto& hook : reset_hooks_) {
        if (hook.rstr_offset != word || (changed & hook.mask) == 0u) continue;
        hook.hold((value & hook.mask) != 0u);
        changed &= ~hook.mask;
    }
    if ((changed & value) != 0u) mcu_.Fault("STM32F2 RCC: reset of this peripheral is not modelled", kBase + word);
}

uint32_t Rcc::ClockSourcesInUse(bool pll_on) const {
    const uint32_t pll_input = (regs_[kPllcfgr / 4u] & (1u << 22)) != 0u ? 1u << 16 : 1u;
    const uint32_t sws = (regs_[kCfgr / 4u] >> 2) & 3u;
    uint32_t used = sws == 0u ? 1u : sws == 1u ? 1u << 16 : (1u << 24) | pll_input;
    if (pll_on) used |= pll_input;
    return used;
}

void Rcc::CheckPllConfiguration() const {
    const uint32_t pll = regs_[kPllcfgr / 4u];
    const uint64_t m = pll & 0x3Fu;
    const uint64_t n = (pll >> 6) & 0x1FFu;
    const uint64_t input = ((pll & (1u << 22)) != 0u ? hse_hz_ : kHsiHz) / (m < 2u ? 1u : m);
    if (m < 2u || n < 192u || n > 432u || input < 1000000u || input > 2000000u || input * n < 192000000u ||
        input * n > 432000000u)
        mcu_.Fault("STM32F2 RCC: a PLL configuration RM0033 calls wrong", pll);
}

uint64_t Rcc::VcoHz() const {
    const uint32_t pll = regs_[kPllcfgr / 4u];
    const uint64_t input = (pll & (1u << 22)) != 0u ? hse_hz_ : kHsiHz;
    const uint64_t m = pll & 0x3Fu;
    if (m == 0u) mcu_.Fault("STM32F2 RCC: PLLM of zero", pll);
    return input * ((pll >> 6) & 0x1FFu) / m;
}

uint64_t Rcc::Pll48Hz() const {
    if ((regs_[kCr / 4u] & (1u << 24)) == 0u) return 0u;
    const uint32_t q = (regs_[kPllcfgr / 4u] >> 24) & 0xFu;
    if (q < 2u) mcu_.Fault("STM32F2 RCC: PLLQ below 2", regs_[kPllcfgr / 4u]);
    return VcoHz() / q;
}

uint64_t Rcc::SysclkHz() const {
    const uint32_t cfgr = regs_[kCfgr / 4u];
    const uint32_t pll = regs_[kPllcfgr / 4u];
    switch ((cfgr >> 2) & 3u) {
    case 0u: return kHsiHz;
    case 1u: return hse_hz_;
    case 2u: return VcoHz() / ((((pll >> 16) & 3u) + 1u) * 2u);
    default: mcu_.Fault("STM32F2 RCC: reserved SWS value", cfgr);
    }
    CERF_UNREACHABLE();
}

void Rcc::Recompute() {
    static constexpr uint32_t kDivisors[8] = {2u, 4u, 8u, 16u, 64u, 128u, 256u, 512u};
    const uint32_t hpre = (regs_[kCfgr / 4u] >> 4) & 0xFu;
    hclk_hz_ = (hpre & 8u) == 0u ? SysclkHz() : SysclkHz() / kDivisors[hpre & 7u];
    for (uint32_t apb = 0u; apb < 2u; ++apb) {
        const uint32_t ppre = (regs_[kCfgr / 4u] >> (apb != 0u ? 13u : 10u)) & 7u;
        pclk_hz_[apb] = (ppre & 4u) == 0u ? hclk_hz_ : hclk_hz_ / (2u << (ppre & 3u));
        timer_hz_[apb] = (ppre & 4u) == 0u ? hclk_hz_ : pclk_hz_[apb] * 2u;
    }
}

void Rcc::SaveState(StateWriter& writer) const {
    writer.Write("stm32f2_rcc", regs_);
}

void Rcc::RestoreState(StateReader& reader) {
    reader.Read("stm32f2_rcc", regs_);
    Recompute();
}

}

}
