#pragma once

#include "siemens_simatic_mobile_panels_gen2_f_module_bootloader.h"
#include "siemens_simatic_mobile_panels_gen2_f_module_temperature_sensor.h"
#include "../../socs/stm32f2/stm32f2_dma.h"
#include "../../socs/stm32f2/stm32f2_exti.h"
#include "../../socs/stm32f2/stm32f2_flash_interface.h"
#include "../../socs/stm32f2/stm32f2_gpio.h"
#include "../../socs/stm32f2/stm32f2_i2c.h"
#include "../../socs/stm32f2/stm32f2_iwdg.h"
#include "../../socs/stm32f2/stm32f2_mcu.h"
#include "../../socs/stm32f2/stm32f2_rcc.h"
#include "../../socs/stm32f2/stm32f2_rng.h"
#include "../../socs/stm32f2/stm32f2_spi.h"
#include "../../socs/stm32f2/stm32f2_timer.h"
#include "../../socs/stm32f2/stm32f2_usart.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2 {

class FModuleUsartWire : public stm32f2::UsartLine {
public:
    void Connect(const stm32f2::Usart* sender, stm32f2::Usart* receiver) {
        sender_ = sender;
        receiver_ = receiver;
    }
    void Deliver(uint8_t value) override { pending_.push_back(value); }
    void Flush() {
        for (uint8_t value : pending_) {
            receiver_->Receive(value, *sender_);
        }
        pending_.clear();
    }

private:
    const stm32f2::Usart* sender_ = nullptr;
    stm32f2::Usart* receiver_ = nullptr;
    std::vector<uint8_t> pending_;
};

struct FModuleChannel {
    FModuleChannel(stm32f2::McuHost& host, stm32f2::GpioPins& pins, uint64_t rng_seed);

    FModuleTemperatureSensor sensor;
    FModuleUsartWire wire;
    stm32f2::Mcu mcu;
    stm32f2::Rcc rcc;
    stm32f2::FlashInterface flash;
    stm32f2::Timer tim1;
    stm32f2::Timer tim2;
    stm32f2::Timer tim3;
    stm32f2::Timer tim4;
    stm32f2::Timer tim6;
    stm32f2::Iwdg iwdg;
    std::array<std::unique_ptr<stm32f2::Gpio>, stm32f2::Gpio::kPorts> gpio;
    stm32f2::Syscfg syscfg;
    std::unique_ptr<stm32f2::Exti> exti;
    stm32f2::Rng rng;
    stm32f2::I2c i2c1;
    stm32f2::Dma dma1;
    stm32f2::Dma dma2;
    stm32f2::Spi spi2;
    stm32f2::Usart usart1;
    FModuleBootloader bootloader;

    uint64_t target = 0u;
    uint64_t fraction = 0u;
    uint64_t slice_hz = 0u;
    uint64_t slice_cycles = 0u;
    uint64_t slice_fraction = 0u;
};

}

}
