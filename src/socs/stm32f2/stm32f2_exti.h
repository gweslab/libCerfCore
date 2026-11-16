#pragma once

#include "stm32f2_mcu.h"

#include <array>
#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class Gpio;

class Syscfg : public Peripheral {
public:
    static constexpr uint32_t kBase = 0x40013800u;

    explicit Syscfg(Mcu& mcu) : mcu_(mcu) {}

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;

    uint32_t ExtiPort(uint32_t line) const { return (exticr_[line / 4u] >> (4u * (line % 4u))) & 0xFu; }

private:
    Mcu& mcu_;
    uint32_t memrmp_ = 0u;
    std::array<uint32_t, 4> exticr_{};
};

class Exti : public Peripheral {
public:
    static constexpr uint32_t kBase = 0x40013C00u;

    Exti(Mcu& mcu, Syscfg& syscfg, std::array<Gpio*, 9> gpio) : mcu_(mcu), syscfg_(syscfg), gpio_(gpio) {}

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;
    void Tick(uint32_t hclk_cycles) override;
    void Sample();

private:
    void UpdateIrq();

    Mcu& mcu_;
    Syscfg& syscfg_;
    std::array<Gpio*, 9> gpio_;
    uint32_t imr_ = 0u;
    uint32_t emr_ = 0u;
    uint32_t rtsr_ = 0u;
    uint32_t ftsr_ = 0u;
    static constexpr uint32_t kPrResetValue = 0u;
    uint32_t pr_ = kPrResetValue;
    uint32_t levels_ = 0u;
    uint32_t sampled_ = 0u;
};

}

}
