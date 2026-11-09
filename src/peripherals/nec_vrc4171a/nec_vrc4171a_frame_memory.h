#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace cerfcore {

class NecVrc4171aFrameMemory : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return base_; }
    uint32_t MmioSize() const override { return static_cast<uint32_t>(memory_.size()); }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint64_t ReadDword(uint32_t addr) override;
    void     WriteByte (uint32_t addr, uint8_t  value) override;
    void     WriteHalf (uint32_t addr, uint16_t value) override;
    void     WriteDword(uint32_t addr, uint64_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void           SetDecodedBytes(uint32_t bytes);
    const uint8_t* Data() const { return memory_.data(); }

private:
    uint8_t* At(uint32_t addr);

    uint32_t              base_ = 0u;
    std::vector<uint8_t>  memory_;
    std::atomic<uint32_t> mask_{0u};
};

}
