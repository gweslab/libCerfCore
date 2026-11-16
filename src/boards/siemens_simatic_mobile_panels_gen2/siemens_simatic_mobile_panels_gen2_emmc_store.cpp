#include "siemens_simatic_mobile_panels_gen2_emmc_store.h"

#include "../../storage/disk_image.h"
#include "siemens_simatic_mobile_panels_gen2_pdcfs_layout.h"

#include <algorithm>
#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2_emmc {

namespace {

constexpr uint32_t kSector = 512u;
constexpr uint8_t  kPartitionFat32Chs = 0x0Bu;
constexpr uint8_t  kPartitionFat32Lba = 0x0Cu;
constexpr uint64_t kBootloaderInfoOff = 2048u;
constexpr size_t   kBootloaderVersionOff = 52u;
constexpr size_t   kBootloaderDateOff = 56u;
constexpr uint32_t kBootloaderVersion = 108u;
constexpr uint32_t kBootloaderDate = 0x01032019u;

}

bool ReadCardRange(DiskImage& image, uint64_t offset, size_t size, std::vector<uint8_t>& out) {
    const uint64_t first = offset / kSector;
    const uint64_t last = (offset + size + kSector - 1u) / kSector;
    std::vector<uint8_t> sectors(static_cast<size_t>((last - first) * kSector));
    if (!sectors.empty() && !image.ReadSectors(first, static_cast<uint32_t>(last - first), sectors.data())) return false;
    const size_t skip = static_cast<size_t>(offset - first * kSector);
    out.assign(sectors.begin() + skip, sectors.begin() + skip + size);
    return true;
}

bool WriteCardRange(DiskImage& image, uint64_t offset, const uint8_t* data, size_t size) {
    const uint64_t first = offset / kSector;
    const uint64_t last = (offset + size + kSector - 1u) / kSector;
    if (last == first) return true;
    std::vector<uint8_t> sectors(static_cast<size_t>((last - first) * kSector));
    const size_t skip = static_cast<size_t>(offset - first * kSector);
    if (skip != 0u && !image.ReadSectors(first, 1u, sectors.data())) return false;
    if ((offset + size) % kSector != 0u &&
        !image.ReadSectors(last - 1u, 1u, sectors.data() + sectors.size() - kSector))
        return false;
    std::copy(data, data + size, sectors.begin() + skip);
    return image.WriteSectors(first, static_cast<uint32_t>(last - first), sectors.data());
}

bool CardLayoutValid(DiskImage& image) {
    std::vector<uint8_t> mbr;
    std::vector<uint8_t> bpb;
    if (!ReadCardRange(image, 0u, kSector, mbr) ||
        !ReadCardRange(image, uint64_t(kPartitionLba) * kSector, kSector, bpb))
        return false;
    if (mbr[510] != 0x55u || mbr[511] != 0xAAu) return false;
    if ((mbr[450] != kPartitionFat32Chs && mbr[450] != kPartitionFat32Lba) ||
        cerf::le::U32(mbr.data() + 454u) != kPartitionLba || cerf::le::U32(mbr.data() + 458u) != kPartitionSectors)
        return false;
    return cerf::le::U16(bpb.data() + 11u) == kSector && bpb[13] != 0u && bpb[16] != 0u &&
           cerf::le::U32(bpb.data() + 36u) != 0u && cerf::le::U32(bpb.data() + 44u) >= 2u &&
           bpb[510] == 0x55u && bpb[511] == 0xAAu;
}

bool RefreshFactoryArea(DiskImage& image, const CardInputs& inputs) {
    std::vector<uint8_t> region;
    if (!ReadCardRange(image, kFactoryTableOff, kFactoryRegionBytes, region)) return false;
    EnsureFactoryLayout(region, inputs.hardware_mac, inputs.op_type, inputs.panel);
    if (!WriteCardRange(image, kFactoryTableOff, region.data(), region.size())) return false;
    std::vector<uint8_t> bootloader;
    if (!ReadCardRange(image, kBootloaderInfoOff, kSector, bootloader)) return false;
    cerf::le::Put32(bootloader.data() + kBootloaderVersionOff, kBootloaderVersion);
    cerf::le::Put32(bootloader.data() + kBootloaderDateOff, kBootloaderDate);
    return WriteCardRange(image, kBootloaderInfoOff, bootloader.data(), bootloader.size());
}

bool SeedCard(DiskImage& image, const CardInputs& inputs, std::string& error) {
    std::vector<uint8_t> mbr(kSector, 0u);
    mbr[446] = 0x00u;
    mbr[447] = 0x01u;
    mbr[448] = 0x01u;
    mbr[449] = 0x00u;
    mbr[450] = kPartitionFat32Chs;
    mbr[451] = 0xFEu;
    mbr[452] = 0xFFu;
    mbr[453] = 0xFFu;
    cerf::le::Put32(mbr.data() + 454u, kPartitionLba);
    cerf::le::Put32(mbr.data() + 458u, kPartitionSectors);
    mbr[510] = 0x55u;
    mbr[511] = 0xAAu;
    if (!WriteCardRange(image, 0u, mbr.data(), mbr.size()) || !RefreshFactoryArea(image, inputs)) {
        error = "the partition table or the factory area cannot be written";
        return false;
    }

    const uint64_t window_sectors = kPdcfsReservedSectors + uint64_t(kPdcfsFatCount) * PdcfsFatSectors(kPartitionSectors) +
                                    kPdcfsSectorsPerCluster;
    std::vector<uint8_t> volume(static_cast<size_t>(window_sectors * kSector), 0u);
    if (!FormatPdcfsVolume(volume, kPartitionSectors, error)) return false;
    if (!WriteCardRange(image, uint64_t(kPartitionLba) * kSector, volume.data(), volume.size())) {
        error = "the PDCFS volume cannot be written";
        return false;
    }
    return true;
}

}

}
