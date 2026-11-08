#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <atomic>
#include <cstdint>

namespace cerfcore {

class Imx51Src : public Peripheral, public ResetCauseLatch {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    void LatchColdReset() override;
    void LatchWarmReset() override;
    void LatchWatchdogReset() override;
    void ReportOnlyWarmReset() override;

    bool WarmResetKeepsConfiguration(ResetLineKind kind) const;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void ResetRegisters();
    void WriteScr(uint32_t value);
    void WriteSrsr(uint32_t value);

    uint32_t              scr_  = 0;
    std::atomic<uint32_t> srsr_{0};
    uint32_t              sisr_ = 0;
    uint32_t              simr_ = 0;
};

}
