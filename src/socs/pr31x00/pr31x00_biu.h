#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <array>
#include <cstdint>

namespace cerfcore {

/* Philips PR31x00 Bus Interface Unit, TMPR3911/3912 ch.4. Memory Configuration
   registers $000-$020. */
class Pr31x00Biu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x10C00000u; }
    uint32_t MmioSize() const override { return 0x24u; }   /* $000-$023 */

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 BIU ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 BIU ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 BIU WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 BIU WriteHalf", addr, v); }

    /* An access to a Card window reaches I/O space while CARDnIOEN is set and
       Attribute space while it is clear (§4.2.1). */
    bool Card1IoSpace() const;
    bool Card2IoSpace() const;

    void PresetBanks(uint32_t banks);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void WriteConfig0(uint32_t addr, uint32_t value);
    void WriteConfig3(uint32_t addr, uint32_t value);
    void WriteConfig4(uint32_t addr, uint32_t value);
    void ApplyCsPortSizes();
    void ApplyUserWindows();

    static constexpr uint32_t kRegs = 5;   /* MEM_CONFIG0..MEM_CONFIG4 */
    static constexpr uint32_t kCsWindows = 8;

    uint32_t cs_ports_[kCsWindows] = {};

    static constexpr std::array<uint32_t, kRegs> kResetRegs = {1u, 0u, 0xFFu, 0u, 1u << 16};

    std::array<uint32_t, kRegs> reg_ = kResetRegs;
};

}
