#pragma once

#include "stm32f2_mcu.h"

#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class Rcc;

class Iwdg : public Peripheral {
public:
    static constexpr uint32_t kBase = 0x40003000u;

    Iwdg(Mcu& mcu, Rcc& rcc) : mcu_(mcu), rcc_(rcc) {}

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;
    void Tick(uint32_t hclk_cycles) override;
    void ClockChanged() override;

private:
    static constexpr uint64_t kPrescalerStates = 256u;

    void UpdateDeadline();
    uint64_t Divider() const;

    Mcu& mcu_;
    Rcc& rcc_;
    uint32_t pr_ = 0u;
    uint32_t rlr_ = 0x0FFFu;
    uint32_t counter_ = 0x0FFFu;
    uint64_t lsi_remainder_ = 0u;
    uint64_t remainder_hz_ = 0u;
    uint32_t prescaler_ = 0u;
    bool running_ = false;
    bool unlocked_ = false;
    bool expired_ = false;
};

}

}
