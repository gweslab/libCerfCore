#pragma once

#include "../raster_scan_peripheral.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class Imx6Ipu : public RasterScanPeripheral {
public:
    using RasterScanPeripheral::RasterScanPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint8_t ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;
    void WriteByte(uint32_t addr, uint8_t value) override;
    void WriteHalf(uint32_t addr, uint16_t value) override;
    void WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;
    void PostResetEffects();
    bool IdmacChannelEnabled(uint32_t ch) const;
    bool IdmacChannelReady(uint32_t ch) const;
    uint32_t RegisterValue(uint32_t offset) const;

protected:
    ScanShape ScanShapeLocked() const override;
    void      FrameEdgeLocked(uint32_t edge_index) override;
    void      ScanEdgesRan() override {}

private:
    uint32_t ActiveDiBase() const;
    void     WriteConf(uint32_t value);
    void     WriteDi(uint32_t ipu_off, uint32_t value);
    void WriteMerged(uint32_t off, uint32_t value, uint32_t write_mask);
    uint32_t ReadRegisterValue(uint32_t ipu_off) const;
    void EnsureModelledRead(const char* what, uint32_t addr, uint32_t ipu_off) const;
    void EnsureModelledWrite(const char* what, uint32_t addr, uint32_t ipu_off, uint32_t value) const;
    void ResetDisplayBlockDefaults();
    void Reset();
    uint32_t EnabledMaskForChannel(uint32_t ch) const;
    uint32_t ReadyMaskForChannel(uint32_t ch) const;
    void UpdateCpmemCurrentBuffer(uint32_t ipu_off, uint32_t mask, uint32_t buffer);
    std::vector<uint32_t> regs_;
    bool dp_srm_update_pending_ = false;
};

}
