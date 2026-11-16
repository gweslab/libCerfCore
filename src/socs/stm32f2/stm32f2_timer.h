#pragma once

#include "stm32f2_mcu.h"

#include <array>
#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class Rcc;

class Timer : public Peripheral {
public:
    static constexpr uint32_t kNoIrq = 0xFFFFFFFFu;

    Timer(Mcu& mcu, Rcc& rcc, uint32_t base, bool apb2, uint32_t irq)
        : mcu_(mcu), rcc_(rcc), base_(base), apb2_(apb2), irq_(irq) {
        if (Wide()) state_.arr = state_.arr_shadow = 0xFFFFFFFFu;
    }

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;
    void Tick(uint32_t hclk_cycles) override;
    uint64_t CyclesToEvent() const override;
    void ClockChanged() override;

    bool OutputReference(uint32_t channel) const { return (state_.oc_ref >> channel) & 1u; }

    bool OutputEnabled(uint32_t channel) const {
        return ((state_.ccer >> (4u * channel)) & 1u) != 0u && (!Advanced() || (state_.bdtr & 0x8000u) != 0u);
    }
    bool OutputLevel(uint32_t channel) const {
        return OutputReference(channel) != (((state_.ccer >> (4u * channel + 1u)) & 1u) != 0u);
    }

private:
    struct State {
        uint32_t cr1 = 0u;
        uint32_t dier = 0u;
        uint32_t sr = 0u;
        uint32_t ccmr1 = 0u;
        uint32_t ccmr2 = 0u;
        uint32_t ccer = 0u;
        uint32_t cnt = 0u;
        uint32_t psc = 0u;
        uint32_t arr = 0xFFFFu;
        uint32_t bdtr = 0u;
        uint32_t bdtr_written = 0u;
        std::array<uint32_t, 4> ccr{};
        uint32_t psc_shadow = 0u;
        uint32_t arr_shadow = 0xFFFFu;
        uint32_t prescale_count = 0u;
        uint32_t oc_ref = 0u;
        uint64_t clock_remainder = 0u;
        uint64_t remainder_hz = 0u;
    };

    uint32_t LockLevel() const { return (state_.bdtr >> 8) & 3u; }
    bool Advanced() const { return base_ == 0x40010000u || base_ == 0x40010400u; }
    bool Basic() const { return base_ == 0x40001000u || base_ == 0x40001400u; }
    bool Wide() const { return base_ == 0x40000000u || base_ == 0x40000C00u; }
    bool Reserved(uint32_t offset) const;
    void ApplyLevelModes(uint32_t cnt);
    [[noreturn]] void Unmodelled(const char* what, uint32_t detail);
    void Count(uint64_t counts);
    void UpdateEvent();
    void Compare(uint32_t first, uint32_t last);
    void UpdateIrq();

    Mcu& mcu_;
    Rcc& rcc_;
    uint32_t base_;
    bool apb2_;
    uint32_t irq_;
    State state_;
};

}

}
