#include "siemens_simatic_mobile_panels_gen2_f_module_channel.h"
#include "siemens_simatic_mobile_panels_gen2_f_module_clock_gates.h"
#include "siemens_simatic_mobile_panels_gen2_f_module_hardware.h"

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2 {

namespace {

constexpr uint32_t kTim3Irq = 29u;
constexpr uint32_t kTim6Irq = 54u;

}

FModuleChannel::FModuleChannel(stm32f2::McuHost& host, stm32f2::GpioPins& pins, uint64_t rng_seed)
    : sensor(mcu), mcu(host), rcc(mcu, FModuleHardware::kHseHz), flash(mcu, rcc),
      tim1(mcu, rcc, 0x40010000u, true, stm32f2::Timer::kNoIrq),
      tim2(mcu, rcc, 0x40000000u, false, stm32f2::Timer::kNoIrq),
      tim3(mcu, rcc, 0x40000400u, false, kTim3Irq),
      tim4(mcu, rcc, 0x40000800u, false, stm32f2::Timer::kNoIrq),
      tim6(mcu, rcc, 0x40001000u, false, kTim6Irq), iwdg(mcu, rcc), syscfg(mcu), rng(mcu, rcc, rng_seed),
      i2c1(mcu, rcc, 0x40005400u, &sensor), dma1(mcu, stm32f2::Dma::kDma1Base),
      dma2(mcu, stm32f2::Dma::kDma2Base), spi2(mcu, 0x40003800u, dma1, 3u, 4u, 0u),
      usart1(mcu, rcc, 0x40011000u, true, dma2, 5u, 7u, 4u), bootloader(mcu) {
    std::array<stm32f2::Gpio*, stm32f2::Gpio::kPorts> ports{};
    for (uint32_t p = 0u; p < stm32f2::Gpio::kPorts; ++p) {
        gpio[p] = std::make_unique<stm32f2::Gpio>(mcu, pins, p);
        ports[p] = gpio[p].get();
    }
    exti = std::make_unique<stm32f2::Exti>(mcu, syscfg, ports);
    mcu.AddPeripheral(stm32f2::Rcc::kBase, &rcc);
    mcu.AddPeripheral(stm32f2::FlashInterface::kBase, &flash);
    mcu.AddPeripheral(0x40010000u, &tim1);
    mcu.AddPeripheral(0x40000000u, &tim2);
    mcu.AddPeripheral(0x40000400u, &tim3);
    mcu.AddPeripheral(0x40000800u, &tim4);
    mcu.AddPeripheral(0x40001000u, &tim6);
    mcu.AddPeripheral(stm32f2::Iwdg::kBase, &iwdg);
    for (uint32_t p = 0u; p < stm32f2::Gpio::kPorts; ++p)
        mcu.AddPeripheral(stm32f2::Gpio::kBaseA + p * stm32f2::Gpio::kStride, gpio[p].get());
    mcu.AddPeripheral(stm32f2::Syscfg::kBase, &syscfg);
    mcu.AddPeripheral(stm32f2::Exti::kBase, exti.get());
    mcu.AddPeripheral(stm32f2::Rng::kBase, &rng);
    mcu.AddPeripheral(0x40005400u, &i2c1);
    mcu.AddPeripheral(stm32f2::Dma::kDma1Base, &dma1);
    mcu.AddPeripheral(stm32f2::Dma::kDma2Base, &dma2);
    mcu.AddPeripheral(0x40003800u, &spi2);
    mcu.AddPeripheral(0x40011000u, &usart1);
    usart1.ConnectTx(&wire);
    GateFModuleClocks(mcu, rcc);
    rcc.OnPeripheralReset(0x20u, 21u, [this](bool held) { i2c1.SetHeldInReset(held); });
}

}

}
