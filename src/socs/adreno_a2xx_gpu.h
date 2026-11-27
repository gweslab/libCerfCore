#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "adreno_a2xx_mmu.h"

#include <cstdint>
#include <unordered_map>

namespace cerfcore {

class AdrenoA2xxGpu : public Peripheral {
public:
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint32_t ReadWord(uint32_t a) override;
    void     WriteWord(uint32_t a, uint32_t v) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    virtual void RequireCoreRunning(const char* action) = 0;
    virtual void RequireGmemRunning(const char* action) = 0;

    uint32_t      ReadRegister(uint32_t idx);
    void          WriteRegister(uint32_t idx, uint32_t value);
    const AdrenoA2xxMmu& MmuConfig() const;
    void          RaiseCpInterrupt(uint32_t bits, uint32_t address);
    uint32_t      GmemBytes() const;

    std::unordered_map<uint32_t, uint32_t>& RegisterFile();

protected:
    AdrenoA2xxGpu(CerfEmulator& emu, uint32_t base, uint32_t size, int irq_line, uint32_t gmem_bytes);

    void PowerDown();

private:
    void     PublishInterruptLine();
    uint32_t StoredRegister(uint32_t idx) const;
    void     WriteMmuRegister(uint32_t idx, uint32_t value);
    void     RefreshMmu();
    void     WriteInterruptRegister(uint32_t idx, uint32_t value);

    const uint32_t base_;
    const uint32_t size_;
    const int      irq_line_;
    const uint32_t gmem_bytes_;

    uint32_t pm_override1_ = 0;
    uint32_t pm_override2_ = 0;
    std::unordered_map<uint32_t, uint32_t> reg_file_;
    AdrenoA2xxMmu mmu_{};
    bool written_ = false;
};

}
