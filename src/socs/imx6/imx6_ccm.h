#pragma once

#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/core/callback_list.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../freescale_timer_clocks.h"
#include "imx6_mmio_lane.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class Imx6Anatop;

class Imx6Ccm : public Peripheral {
public:
    using Peripheral::Peripheral;

    static constexpr uint64_t kCkilHz = 32768u;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x020C4000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t  ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    bool     ClockRunsIn(uint32_t ccgr, uint32_t index, FreescaleLowPowerMode mode) const;
    uint32_t WfiLowPowerMode() const;
    uint64_t IpuHspHz() const;
    uint64_t IpuDiHz(uint32_t di) const;
    uint64_t GpuCoreHz(bool gpu3d) const;
    GuestCycleClock::Rate UartClockRootRate() const;
    uint64_t ArmClockHz() const;

    void RegisterGateListener(std::function<void()> fn);
    void RegisterRateListener(std::function<void()> fn);

private:
    static bool IsRegister(uint32_t off);
    static bool IsWritableRegister(uint32_t off);

    void ResetRegisters();
    void Reset();
    void WriteLane(const Imx6MmioLane& lane);
    void     NotifyGates();
    void     NotifyRates();
    void     ApplyArmClock();
    uint64_t PeriphHz() const;
    uint64_t MmdcCh0Hz() const;
    uint64_t AxiHz() const;

    CallbackList<>                     gate_listeners_;
    CallbackList<>                     rate_listeners_;
    Imx6Anatop*                        anatop_ = nullptr;
    uint32_t                           regs_[0x8Cu / 4u]{};
};

}
