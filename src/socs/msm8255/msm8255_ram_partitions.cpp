#include "msm8255_ram_partitions.h"

namespace cerfcore {

bool Msm8255PartitionIsKernelRam(const Msm8255RamPartition& part) {
    return part.attr == kMsm8255RamAttrReadWrite && part.domain == kMsm8255RamDomainApps &&
           (part.type == kMsm8255RamTypeNone || part.type == kMsm8255RamTypeAppsMemory) &&
           part.category >= kMsm8255RamCategoryEbi0Cs0 && part.category <= kMsm8255RamCategoryEbi1Cs1;
}

}
