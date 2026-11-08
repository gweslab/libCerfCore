#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class FreescaleModuleClocks;

class Imx51Nfc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte (uint32_t addr, uint8_t  value) override;
    void     WriteHalf (uint32_t addr, uint16_t value) override;
    void     WriteWord (uint32_t addr, uint32_t value) override;

    uint32_t NfcRead (uint32_t addr, uint32_t width);
    void     NfcWrite(uint32_t addr, uint32_t value, uint32_t width);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    static constexpr uint32_t kCfg2Reset      = 0x0040223Du;
    static constexpr uint32_t kCfg3Reset      = 0x00020600u;
    static constexpr uint32_t kDelayLineReset = 0x00000010u;
    static constexpr uint32_t kWrProtectReset = 0x49249200u;

    void     ResetRegisters(ResetLineKind kind);
    void     Launch(uint32_t value);
    void     RunHeldLaunch();
    void     ExecuteLaunch(uint32_t value);
    void     ReadPage();
    void     ReadStatus();
    void     AutoRead();
    void     AutoProg();
    void     AutoErase();
    void     FillPageBuffer(uint64_t flash_off);
    void     ReadId();
    uint64_t FlashOffset() const;
    uint64_t AutoFlashOffset() const;
    std::array<uint8_t, 5> AutoAddrBytes() const;

    std::array<uint8_t, 0x200> spare_{};
    uint8_t  nand_cmd_  = 0;
    std::array<uint32_t, 12> nand_add_{};
    uint32_t cfg1_       = 0;
    uint32_t ecc_status_ = 0;
    uint32_t status_sum_ = 0;
    uint32_t launch_     = 0;
    bool     launch_held_ = false;

    uint32_t wr_protect_ = kWrProtectReset;
    std::array<uint32_t, 8> unlock_{};
    uint32_t cfg2_       = kCfg2Reset;
    uint32_t cfg3_       = kCfg3Reset;
    uint32_t axi_error_  = 0;
    uint32_t delay_line_ = kDelayLineReset;

    bool     int_pending_ = false;
    bool     creq_        = false;

    std::array<uint8_t, 5> addr_bytes_{};
    uint32_t addr_idx_ = 0;

    uint64_t seq_data_off_  = 0;
    uint64_t seq_cache_off_ = 0;

    FreescaleModuleClocks* module_clocks_ = nullptr;
};

}
