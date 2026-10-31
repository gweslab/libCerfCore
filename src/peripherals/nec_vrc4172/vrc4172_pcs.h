#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

namespace cerfcore {

class Vrc4172Pcs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;

    bool Selects(uint32_t channel, uint32_t pa) const;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    static constexpr uint32_t kChannels       = 6u;
    static constexpr uint32_t kRegsPerChannel = 4u;
    static constexpr uint32_t kRegCount       = kChannels * kRegsPerChannel;

    void ApplyReset();

    uint16_t regs_[kRegCount] = {};
};

}
