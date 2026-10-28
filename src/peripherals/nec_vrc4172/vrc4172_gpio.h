#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>
#include <mutex>

namespace cerfcore {

class Vrc4172Gpio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x15001080u; }
    uint32_t MmioSize() const override { return 0x4Cu; }

    uint8_t  ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint32_t ReadWord(uint32_t addr) override { HaltUnsupportedAccess("VRC4172 GPIO ReadWord", addr, 0); }
    void     WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("VRC4172 GPIO WriteWord", addr, v); }

    void SetPinLevel(int pin, bool level);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    struct Polarity {
        bool high;
        bool both_edges;
    };

    uint16_t ReadReg16Locked(uint32_t off);
    void     DriveGiuLocked();
    void     EvaluateStatusLocked();
    Polarity PolarityOf(int pin) const;

    mutable std::mutex mtx_;

    uint16_t dir_     = 0;
    uint16_t inten_   = 0;
    uint16_t intst_   = 0;
    uint16_t intclr_  = 0;
    uint16_t inttyp_  = 0;
    uint16_t intlv0l_ = 0;
    uint16_t intlv0h_ = 0;

    uint16_t level_ = 0;

    int  giu_pin_      = 0;
    bool giu_asserted_ = false;
};

}
