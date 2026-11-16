#include "siemens_simatic_mobile_panels_gen2_pdcfs_layout.h"

#include <cstring>
#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2_emmc {

namespace {

constexpr uint32_t kSector = 512u;
constexpr uint32_t kRootCluster = 2u;
constexpr uint16_t kFsInfoSector = 1u;
constexpr uint16_t kBackupBootSector = 6u;
constexpr uint32_t kEndOfChain = 0x0FFFFFFFu;

}

uint32_t PdcfsFatSectors(uint32_t volume_sectors) {
    const uint32_t tmp2 = ((256u * kPdcfsSectorsPerCluster) + kPdcfsFatCount) / 2u;
    return (volume_sectors - kPdcfsReservedSectors + tmp2 - 1u) / tmp2;
}

bool FormatPdcfsVolume(std::vector<uint8_t>& data, uint32_t volume_sectors, std::string& error) {
    const uint32_t fat_sectors = PdcfsFatSectors(volume_sectors);
    const uint32_t cluster_bytes = kPdcfsSectorsPerCluster * kSector;
    const uint64_t fat0_off = uint64_t(kPdcfsReservedSectors) * kSector;
    const uint64_t data_off = uint64_t(kPdcfsReservedSectors + kPdcfsFatCount * fat_sectors) * kSector;
    if (data.size() < data_off + cluster_bytes) {
        error = "the PDCFS seeding window does not reach the root directory";
        return false;
    }

    uint8_t* bpb = data.data();
    bpb[0] = 0xEBu;
    bpb[1] = 0x58u;
    bpb[2] = 0x90u;
    std::memcpy(bpb + 3u, "MSDOS5.0", 8u);
    cerf::le::Put16(bpb + 11u, static_cast<uint16_t>(kSector));
    bpb[13] = static_cast<uint8_t>(kPdcfsSectorsPerCluster);
    cerf::le::Put16(bpb + 14u, static_cast<uint16_t>(kPdcfsReservedSectors));
    bpb[16] = static_cast<uint8_t>(kPdcfsFatCount);
    bpb[21] = 0xF8u;
    cerf::le::Put32(bpb + 32u, volume_sectors);
    cerf::le::Put32(bpb + 36u, fat_sectors);
    cerf::le::Put32(bpb + 44u, kRootCluster);
    cerf::le::Put16(bpb + 48u, kFsInfoSector);
    cerf::le::Put16(bpb + 50u, kBackupBootSector);
    bpb[64] = 0x80u;
    bpb[66] = 0x29u;
    std::memcpy(bpb + 71u, "           ", 11u);
    std::memcpy(bpb + 82u, "FAT32   ", 8u);
    bpb[510] = 0x55u;
    bpb[511] = 0xAAu;

    const auto put_fat = [&](uint32_t clus, uint32_t value) {
        for (uint32_t copy = 0; copy < kPdcfsFatCount; ++copy)
            cerf::le::Put32(data.data() + fat0_off + uint64_t(copy) * fat_sectors * kSector + uint64_t(clus) * 4u,
                            value);
    };
    put_fat(0u, 0x0FFFFFF8u);
    put_fat(1u, kEndOfChain);
    put_fat(kRootCluster, kEndOfChain);

    const uint32_t total_clusters =
        (volume_sectors - kPdcfsReservedSectors - kPdcfsFatCount * fat_sectors) / kPdcfsSectorsPerCluster;

    const auto write_fsinfo = [&](uint32_t sector) {
        uint8_t* fsi = data.data() + uint64_t(sector) * kSector;
        std::memset(fsi, 0, kSector);
        cerf::le::Put32(fsi + 0x000u, 0x41615252u);
        cerf::le::Put32(fsi + 0x1E4u, 0x61417272u);
        cerf::le::Put32(fsi + 0x1E8u, total_clusters - 1u);
        cerf::le::Put32(fsi + 0x1ECu, kRootCluster + 1u);
        cerf::le::Put32(fsi + 0x1FCu, 0xAA550000u);
    };
    write_fsinfo(kFsInfoSector);
    std::memcpy(data.data() + uint64_t(kBackupBootSector) * kSector, bpb, kSector);
    write_fsinfo(kBackupBootSector + kFsInfoSector);
    return true;
}

}

}
