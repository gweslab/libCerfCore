#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "imx6_variant.h"

#include <bitset>
#include <cstdint>

namespace cerfcore {

enum class Imx6WdogPadSignal { WdogB, ResetBDeb };

struct Imx6WdogPadMux {
    const char* pad_name;
    Imx6WdogPadSignal signal;
    uint8_t bank;
    uint8_t pin;
    uint8_t mux_mode;
    uint8_t unit;
};

class Imx6Iomuxc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x020E0000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;
    void WriteByte(uint32_t addr, uint8_t value) override;
    void WriteHalf(uint32_t addr, uint16_t value) override;
    void WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    uint32_t PadRegister(uint32_t off) const;
    const Imx6WdogPadMux* WdogPadMux(uint32_t wdog, Imx6WdogPadSignal signal) const;

    static constexpr uint32_t kGpr1 = 0x004u;
    static constexpr uint32_t kGpr2 = 0x008u;

private:
    static constexpr uint32_t kGprEnd = 0x038u;
    static constexpr uint32_t kPadMuxSion = 1u << 4;
    static constexpr uint32_t kGpr2Modelled = 0x000007EFu;

    void CheckGprWrite(uint32_t addr, uint32_t off, uint32_t value);
    bool IsRegister(uint32_t off) const;
    void ResetRegisters();
    void Reset();

    static constexpr uint32_t kWords = 0x4000u / 4u;
    Imx6Family family_ = Imx6Family::DualQuad;
    uint32_t regs_[kWords]{};
    std::bitset<kWords> known_;
};

}
