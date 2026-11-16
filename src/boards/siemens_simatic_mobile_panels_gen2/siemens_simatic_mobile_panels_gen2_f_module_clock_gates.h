#pragma once

#include "../../socs/stm32f2/stm32f2_dma.h"
#include "../../socs/stm32f2/stm32f2_exti.h"
#include "../../socs/stm32f2/stm32f2_gpio.h"
#include "../../socs/stm32f2/stm32f2_mcu.h"
#include "../../socs/stm32f2/stm32f2_rcc.h"
#include "../../socs/stm32f2/stm32f2_rng.h"

#include <cstdint>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2 {

inline void GateFModuleClocks(stm32f2::Mcu& mcu, const stm32f2::Rcc& rcc) {
    const auto gate = [&mcu, &rcc](uint32_t base, uint32_t enr, uint32_t bit) {
        mcu.GateClock(base, [&rcc, enr, bit] { return rcc.ClockEnabled(enr, bit); });
    };
    for (uint32_t p = 0u; p < stm32f2::Gpio::kPorts; ++p) gate(stm32f2::Gpio::kBaseA + p * stm32f2::Gpio::kStride, 0x30u, p);
    gate(stm32f2::Dma::kDma1Base, 0x30u, 21u);
    gate(stm32f2::Dma::kDma2Base, 0x30u, 22u);
    gate(stm32f2::Rng::kBase, 0x34u, 6u);
    gate(0x40000000u, 0x40u, 0u);
    gate(0x40000400u, 0x40u, 1u);
    gate(0x40000800u, 0x40u, 2u);
    gate(0x40001000u, 0x40u, 4u);
    gate(0x40003800u, 0x40u, 14u);
    gate(0x40005400u, 0x40u, 21u);
    gate(0x40010000u, 0x44u, 0u);
    gate(0x40011000u, 0x44u, 4u);
    gate(stm32f2::Syscfg::kBase, 0x44u, 14u);
}

}

}
