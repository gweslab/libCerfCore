#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/host/guest_deep_sleep.h>

#include <cstdint>
#include <atomic>

namespace cerfcore {

class Imx6Gpc : public Peripheral, public DeepSleepWaker {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x020DC000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadHalf(uint32_t address) override;
    uint32_t ReadWord(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteHalf(uint32_t address, uint16_t value) override;
    void WriteWord(uint32_t address, uint32_t value) override;

    void SaveState(StateWriter& writer) override;
    void RestoreState(StateReader& reader) override;
    bool AllowsWake(uint32_t irq) const;
    void LatchSleepWakeCause() override;
    void ClearSleepWakeCause() override;

private:
    void Reset();
    void OnIdle();
    bool HasWakePending() const;
    uint32_t ReadRegister(uint32_t offset);
    void WriteRegister(uint32_t offset, uint32_t value);

    std::atomic<uint32_t> imr_[4]{};
    std::atomic<uint32_t> cntr_irq_mask_{0u};
    std::atomic<bool> in_low_power_{false};
    std::atomic<bool> clock_gated_{false};
    uint32_t pgr_ = 0u;
    uint32_t pgc_gpu_ctrl_ = 0u;
    uint32_t pgc_gpu_pupscr_ = 0u;
    uint32_t pgc_gpu_pdn_scr_ = 0u;
    uint32_t pgc_gpu_sr_ = 0u;
    uint32_t pgc_cpu_ctrl_ = 0u;
    uint32_t pgc_cpu_sr_ = 0u;
};

}
