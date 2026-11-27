#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

inline constexpr uint32_t kMsm8255RamAttrReadOnly    = 0u;
inline constexpr uint32_t kMsm8255RamAttrReadWrite   = 1u;
inline constexpr uint32_t kMsm8255RamCategoryDefault = ~0u;
inline constexpr uint32_t kMsm8255RamCategoryImem    = 5u;
inline constexpr uint32_t kMsm8255RamCategoryEbi0Cs0 = 6u;
inline constexpr uint32_t kMsm8255RamCategoryEbi1Cs0 = 8u;
inline constexpr uint32_t kMsm8255RamCategoryEbi1Cs1 = 9u;
inline constexpr uint32_t kMsm8255RamDomainApps      = 1u;
inline constexpr uint32_t kMsm8255RamDomainShared    = 3u;
inline constexpr uint32_t kMsm8255RamTypeNone        = 0u;
inline constexpr uint32_t kMsm8255RamTypeBootRegion1 = 2u;
inline constexpr uint32_t kMsm8255RamTypeAppsMemory  = 5u;

struct Msm8255RamPartition {
    uint32_t start;
    uint32_t size;
    uint32_t attr;
    uint32_t category;
    uint32_t domain;
    uint32_t type;
};

bool Msm8255PartitionIsKernelRam(const Msm8255RamPartition& part);

class Msm8255RamPartitions : public Service {
public:
    using Service::Service;

    virtual uint32_t PartitionCount() const = 0;
    virtual Msm8255RamPartition Partition(uint32_t index) const = 0;
};

}
