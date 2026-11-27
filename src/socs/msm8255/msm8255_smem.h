#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

inline constexpr uint32_t kMsm8255SmemPa   = 0x00100000u;
inline constexpr uint32_t kMsm8255SmemBytes = 0x00100000u;

inline constexpr char kMsm8255DalI2c1Lock[] = "I2C02000021";
inline constexpr char kMsm8255DalI2c2Lock[] = "I2C02000022";

class Msm8255Smem : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t SmemPa();
    uint32_t ItemPa(uint32_t id, uint32_t bytes);
    bool     ItemPaAndSize(uint32_t id, uint32_t& pa, uint32_t& bytes);
    uint32_t DalRecordPa(const char* name);

private:
    uint32_t AppendDalRecord(uint32_t rec, const char* name, uint32_t bytes,
                             uint32_t flags);
    uint32_t TocEntryPa(uint32_t id);
    bool     ReadTocEntry(uint32_t id, uint32_t& off, uint32_t& size);
    void Seed();
    uint32_t RecordPa(uint32_t index) const;
    void SeedSpeedRecord();
    void SeedPerfLevels();
    void SeedAvsConfig();
    void SeedVoltageSwitch();
    void WriteDalRecordName(uint32_t rec, const char* name);
    void SeedDalGlobalContext();
    void PublishRamPartitions();
    void PublishItem(uint32_t id, uint32_t off, uint32_t size, uint32_t magic);
};

}
