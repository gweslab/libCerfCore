#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>
#include <mutex>

namespace cerfcore {

class OdoDmaPointerPair : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioSize() const override;

    uint32_t GetEffectivePa();

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    static constexpr uint32_t kSlotDmaLow  = 0x00u;
    static constexpr uint32_t kSlotDmaHigh = 0x04u;

    static bool IsSlot(uint32_t off) { return off == kSlotDmaLow || off == kSlotDmaHigh; }

    void     Store(uint32_t off, uint16_t value);
    uint16_t Load(uint32_t off);

private:
    mutable std::mutex state_mutex_;
    uint16_t           dma_low_  = 0;
    uint16_t           dma_high_ = 0;
};

}
