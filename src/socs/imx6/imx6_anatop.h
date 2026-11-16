#pragma once

#include <cerfcore/core/callback_list.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "imx6_mmio_lane.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

constexpr uint32_t kImx6PllLock = 1u << 31;
constexpr uint32_t kImx6PllEnable = 1u << 13;
constexpr uint64_t kImx6RefHz = 24000000u;

class Imx6Anatop : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x020C8000u; }
    uint32_t MmioSize() const override { return 0x1000u; }

    uint8_t  ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    uint64_t Pll2Hz() const;
    uint64_t Pll2BypassHz() const;
    uint64_t Pll3Hz() const;
    uint64_t PllArmHz() const;
    uint64_t Pll5Hz() const;
    uint64_t Pll2PfdHz(uint32_t pfd) const;
    uint64_t Pll3PfdHz(uint32_t pfd) const;

    void RegisterRateListener(std::function<void()> fn);

private:
    static bool IsPllControl(uint32_t base);
    static bool IsPllPowered(uint32_t base, uint32_t reg);
    static void UpdatePllLockAfterWrite(uint32_t base, uint32_t& reg);
    static bool HasAliases(uint32_t base);
    static bool IsSingleRegister(uint32_t off);
    static bool IsRegister(uint32_t off);
    static bool IsWritableRegister(uint32_t off);

    void     Reset();
    void     WriteLane(const Imx6MmioLane& lane);
    void     Store(uint32_t base, uint32_t alias, uint32_t mask, uint32_t value);
    uint64_t PllHz(uint32_t base, const char* name) const;
    uint64_t PfdHz(uint32_t reg, uint32_t pfd, uint64_t pll_hz, const char* name) const;
    void     NotifyRates();

    CallbackList<>                     rate_listeners_;
    uint32_t                           regs_[0x270u / 0x10u]{};
};

}
