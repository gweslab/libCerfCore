#include "../../core/board_parts.h"
#include "../../core/ram_generic_part_id.h"
#include "../../socs/msm8255/msm8255_ram_partitions.h"
#include "../../socs/msm8255/msm8255_smem.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nokia_sea_ray_family_id.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

struct Bank {
    uint32_t start;
    uint32_t category;
};

constexpr Bank kBanks[] = {
    { 0x00000000u, kMsm8255RamCategoryEbi0Cs0 },
    { 0x40000000u, kMsm8255RamCategoryEbi1Cs0 },
};

struct Range {
    uint32_t start;
    uint32_t size;
};

constexpr Range kOsblReserved[] = {
    { 0x00000000u,    MB(1)             },
    { 0x04700000u,    MB(45)            },
    { 0x04500000u,    MB(2)             },
    { 0x03E00000u,    MB(7)             },
    { 0x07700000u,    MB(3)             },
    { 0x07400000u,    MB(3)             },
    { kMsm8255SmemPa, kMsm8255SmemBytes },
};

constexpr Range kOsblBootRegion = { 0x04500000u, MB(2) };
constexpr Range kOsblImem       = { 0x80000000u, 0x40000u };

class NokiaSeaRayRamPartitions : public Msm8255RamPartitions {
public:
    using Msm8255RamPartitions::Msm8255RamPartitions;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override {
        const uint32_t ram_bytes  = *emu_.Get<BoardParts>().Find(GenericPartId::Ram)->size_mb << 20;
        const uint32_t bank_bytes = ram_bytes / static_cast<uint32_t>(std::size(kBanks));
        for (const Bank& bank : kBanks) {
            parts_.push_back({ bank.start, bank_bytes, kMsm8255RamAttrReadWrite, bank.category,
                               kMsm8255RamDomainApps, kMsm8255RamTypeAppsMemory });
        }
        parts_.push_back({ kOsblImem.start, kOsblImem.size, kMsm8255RamAttrReadWrite, kMsm8255RamCategoryImem,
                           kMsm8255RamDomainApps, kMsm8255RamTypeAppsMemory });

        const uint32_t boot_category   = CategoryAt(kOsblBootRegion.start);
        const uint32_t shared_category = CategoryAt(kMsm8255SmemPa);
        for (const Range& range : kOsblReserved) Remove(range);

        parts_.push_back({ kOsblBootRegion.start, kOsblBootRegion.size, kMsm8255RamAttrReadWrite,
                           boot_category, kMsm8255RamDomainApps, kMsm8255RamTypeBootRegion1 });
        parts_.push_back({ kMsm8255SmemPa, kMsm8255SmemBytes, kMsm8255RamAttrReadOnly, shared_category,
                           kMsm8255RamDomainShared, kMsm8255RamTypeNone });
    }

    uint32_t PartitionCount() const override { return static_cast<uint32_t>(parts_.size()); }

    Msm8255RamPartition Partition(uint32_t index) const override { return parts_[index]; }

private:
    uint32_t CategoryAt(uint32_t pa) const {
        for (const Msm8255RamPartition& part : parts_) {
            if (pa >= part.start && pa - part.start <= part.size) return part.category;
        }
        return kMsm8255RamCategoryDefault;
    }

    void Remove(const Range& range) {
        const uint64_t range_end = uint64_t(range.start) + range.size;
        for (size_t i = parts_.size(); i-- > 0;) {
            Msm8255RamPartition& part     = parts_[i];
            const uint64_t       part_end = uint64_t(part.start) + part.size;
            if (range.start < part.start || range_end > part_end) continue;

            if (range.start == part.start) {
                if (range_end == part_end) {
                    parts_.erase(parts_.begin() + static_cast<std::ptrdiff_t>(i));
                } else {
                    part.start = static_cast<uint32_t>(range_end);
                    part.size  = static_cast<uint32_t>(part_end - range_end);
                }
                return;
            }

            part.size = range.start - part.start;
            if (range_end < part_end) {
                const Msm8255RamPartition tail{ static_cast<uint32_t>(range_end),
                                                static_cast<uint32_t>(part_end - range_end),
                                                kMsm8255RamAttrReadWrite, part.category,
                                                kMsm8255RamDomainApps, kMsm8255RamTypeAppsMemory };
                parts_.insert(parts_.begin() + static_cast<std::ptrdiff_t>(i) + 1, tail);
            }
            return;
        }
    }

    std::vector<Msm8255RamPartition> parts_;
};

}

REGISTER_SERVICE_AS(NokiaSeaRayRamPartitions, Msm8255RamPartitions);

}
