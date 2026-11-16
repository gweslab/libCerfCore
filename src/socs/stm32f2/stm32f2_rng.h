#pragma once

#include "stm32f2_mcu.h"
#include "stm32f2_rcc.h"

#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class Rng : public Peripheral {
public:
    static constexpr uint32_t kBase = 0x50060800u;

    Rng(Mcu& mcu, Rcc& rcc, uint64_t seed) : mcu_(mcu), rcc_(rcc), state_(seed | 1u) {}

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;

private:
    void CheckClock();

    Mcu& mcu_;
    Rcc& rcc_;
    uint32_t cr_ = 0u;
    uint64_t state_;
};

}

}
