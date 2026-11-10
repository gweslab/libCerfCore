#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include "sh77xx_dmac.h"
#include "sh77xx_intc.h"
#include "sh77xx_standby_control.h"

#include <cstdint>

namespace cerfcore {

class Sh77xxAnalogInputs;

class Sh77xxAdc : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() override;

    uint32_t MmioBase() const override { return 0x04000080u; }
    uint32_t MmioSize() const override { return 0x14u; }

    uint8_t ReadByte(uint32_t addr) override;
    void    WriteByte(uint32_t addr, uint8_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    static constexpr uint32_t kDataRegisters = 4u;

    uint8_t ReadData(uint32_t offset);
    uint8_t ReadControlStatus();
    void    WriteControlStatus(uint8_t value);
    void    Convert(uint8_t value);
    void    ClearEndFlag();
    bool    Interrupting() const;
    void    Drive();
    void    Initialize();

    Sh77xxIntc*         intc_   = nullptr;
    Sh77xxDmac*         dmac_   = nullptr;
    Sh77xxStbcr2*       stbcr2_ = nullptr;
    Sh77xxAnalogInputs* inputs_ = nullptr;
    uint16_t            data_[kDataRegisters]{};
    uint8_t             control_     = 0u;
    uint8_t             trigger_     = 0u;
    uint8_t             temp_        = 0u;
    bool                end_         = false;
    bool                end_read_    = false;
    bool                temp_loaded_ = false;
};

}
