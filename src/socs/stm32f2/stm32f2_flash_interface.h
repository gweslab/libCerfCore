#pragma once

#include "stm32f2_mcu.h"
#include "stm32f2_rcc.h"

#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class FlashInterface : public Peripheral, public FlashProgrammer {
public:
    static constexpr uint32_t kBase = 0x40023C00u;

    FlashInterface(Mcu& mcu, Rcc& rcc) : mcu_(mcu), rcc_(rcc) { mcu_.SetFlashProgrammer(this); }

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;
    void ClockChanged() override;

    void Program(uint32_t address, uint32_t size, uint32_t value) override;

    bool TakeModified() {
        const bool modified = modified_;
        modified_ = false;
        return modified;
    }

private:
    void WriteKey(uint32_t value);
    void WriteOptionKey(uint32_t value);
    void Start();
    void EraseRange(uint32_t offset, uint32_t bytes);
    void CheckLatency();

    Mcu& mcu_;
    Rcc& rcc_;
    uint32_t acr_ = 0u;
    uint32_t sr_ = 0u;
    uint32_t cr_ = 0x80000000u;
    uint32_t optcr_ = 0x0FFFAAEDu;
    uint32_t key_stage_ = 0u;
    uint32_t option_key_stage_ = 0u;
    bool modified_ = false;
};

}

}
