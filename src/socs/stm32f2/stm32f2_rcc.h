#pragma once

#include "stm32f2_mcu.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

namespace stm32f2 {

class Rcc : public Peripheral {
public:
    static constexpr uint32_t kBase = 0x40023800u;

    Rcc(Mcu& mcu, uint32_t hse_hz) : mcu_(mcu), hse_hz_(hse_hz) {}

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;

    static constexpr uint32_t kResetFlags = 0xFE000000u;
    static constexpr uint32_t kPinResetFlag = 1u << 26;
    static constexpr uint32_t kIwdgResetFlag = 1u << 29;
    uint32_t ResetFlags() const { return regs_[kCsr / 4u] & kResetFlags; }
    bool ClockEnabled(uint32_t enr_offset, uint32_t bit) const { return ((regs_[enr_offset / 4u] >> bit) & 1u) != 0u; }
    void SetResetFlags(uint32_t flags) {
        regs_[kCsr / 4u] = (regs_[kCsr / 4u] & ~kResetFlags) | (flags & kResetFlags);
    }

    void OnPeripheralReset(uint32_t rstr_offset, uint32_t bit, std::function<void(bool)> hold) {
        reset_hooks_.push_back({rstr_offset, 1u << bit, std::move(hold)});
    }

    uint64_t HclkHz() const { return hclk_hz_; }
    uint64_t PclkHz(bool apb2) const { return pclk_hz_[apb2 ? 1u : 0u]; }
    uint64_t TimerHz(bool apb2) const { return timer_hz_[apb2 ? 1u : 0u]; }
    uint64_t Pll48Hz() const;

private:
    static constexpr uint32_t kCr = 0x00u;
    static constexpr uint32_t kPllcfgr = 0x04u;
    static constexpr uint32_t kCfgr = 0x08u;
    static constexpr uint32_t kCir = 0x0Cu;
    static constexpr uint32_t kCsr = 0x74u;
    static constexpr uint32_t kRegisterCount = 0x88u / 4u;

    struct ResetHook {
        uint32_t rstr_offset;
        uint32_t mask;
        std::function<void(bool)> hold;
    };

    bool Modelled(uint32_t offset) const;
    void ApplyPeripheralReset(uint32_t word, uint32_t old_value, uint32_t value);
    uint64_t VcoHz() const;
    uint32_t ClockSourcesInUse(bool pll_on) const;
    void CheckPllConfiguration() const;
    uint64_t SysclkHz() const;
    void Recompute();

    Mcu& mcu_;
    uint32_t hse_hz_;
    uint64_t hclk_hz_ = 16000000u;
    uint64_t pclk_hz_[2] = {16000000u, 16000000u};
    uint64_t timer_hz_[2] = {16000000u, 16000000u};
    std::array<uint32_t, kRegisterCount> regs_ = Resets();
    std::vector<ResetHook> reset_hooks_;

    static constexpr uint32_t kHsiCalibration = 0x00u;

    static constexpr std::array<uint32_t, kRegisterCount> Resets() {
        std::array<uint32_t, kRegisterCount> r{};
        r[0x00u / 4u] = (kHsiCalibration << 8) | 0x83u;
        r[0x04u / 4u] = 0x24003010u;
        r[0x50u / 4u] = 0x7E6791FFu;
        r[0x54u / 4u] = 0x000000F1u;
        r[0x58u / 4u] = 0x00000001u;
        r[0x60u / 4u] = 0x36FEC9FFu;
        r[0x64u / 4u] = 0x00075F33u;
        r[0x74u / 4u] = 0x0E000000u;
        r[0x84u / 4u] = 0x20003000u;
        return r;
    }
};

}

}
