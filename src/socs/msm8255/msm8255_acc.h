#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <atomic>
#include <cstdint>

namespace cerfcore {

class Msm8255Acc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    bool AppsPowerDown() const;

private:
    void RequireModeledSource(uint32_t clk_ctl, uint32_t clk_sel);

    std::atomic<uint32_t> clk_ctl_;
    std::atomic<uint32_t> clk_sel_;
    std::atomic<uint32_t> apps_pwrdown_{0u};
};

}
