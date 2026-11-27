#include "fat_volume_layout.h"

#include <cerfcore/core/byte_order.h>

#include <chrono>
#include <cstring>

namespace cerfcore {

namespace fat_volume_layout {

namespace {

size_t MbrPartitionEntryOffset(unsigned index) { return kMbrPartTable + index * kMbrPartEntryBytes; }

}

uint8_t* MbrPartitionEntry(uint8_t* mbr, unsigned index) {
    return mbr + MbrPartitionEntryOffset(index);
}

void WriteMbrPartition(uint8_t* mbr, unsigned index, const MbrPartition& p) {
    uint8_t* e = MbrPartitionEntry(mbr, index);
    e[kPartStatus] = p.status;
    std::memcpy(e + kPartChsFirst, p.chs_first, sizeof(p.chs_first));
    e[kPartType] = p.type;
    std::memcpy(e + kPartChsLast, p.chs_last, sizeof(p.chs_last));
    cerf::le::Put32(e + kPartStartLba, p.start_lba);
    cerf::le::Put32(e + kPartSectors, p.sectors);
}

/* dosfstools src/mkfs.fat.c: BOOT_SIGN 0xAA55 at 0x1FE, also on the FSInfo sector. */
void WriteBootSignature(uint8_t* sector) {
    cerf::le::Put16(sector + kBootSignature, 0xAA55u);
}

bool HasBootSignature(const uint8_t* sector) {
    return cerf::le::U16(sector, kBootSignature) == 0xAA55u;
}

MbrPartition ReadMbrPartition(const uint8_t* mbr, unsigned index) {
    const uint8_t* e = mbr + MbrPartitionEntryOffset(index);
    MbrPartition p;
    p.status = e[kPartStatus];
    std::memcpy(p.chs_first, e + kPartChsFirst, sizeof(p.chs_first));
    p.type = e[kPartType];
    std::memcpy(p.chs_last, e + kPartChsLast, sizeof(p.chs_last));
    p.start_lba = cerf::le::U32(e, kPartStartLba);
    p.sectors   = cerf::le::U32(e, kPartSectors);
    return p;
}

void WriteBpb(uint8_t* bs, const Bpb& b) {
    std::memcpy(bs + kBsJump, b.jump, sizeof(b.jump));
    if (b.oem_name) std::memcpy(bs + kBsOemName, b.oem_name, 8);
    cerf::le::Put16(bs + kBpbBytsPerSec, b.bytes_per_sector);
    bs[kBpbSecPerClus] = b.sectors_per_cluster;
    cerf::le::Put16(bs + kBpbRsvdSecCnt, b.reserved_sectors);
    bs[kBpbNumFats] = b.num_fats;
    cerf::le::Put16(bs + kBpbRootEntCnt, b.root_entries);
    cerf::le::Put16(bs + kBpbTotSec16, b.total_sectors16);
    bs[kBpbMedia] = b.media;
    cerf::le::Put16(bs + kBpbFatSz16, b.fat_size16);
    cerf::le::Put16(bs + kBpbSecPerTrk, b.sectors_per_track);
    cerf::le::Put16(bs + kBpbNumHeads, b.num_heads);
    cerf::le::Put32(bs + kBpbHiddSec, b.hidden_sectors);
    cerf::le::Put32(bs + kBpbTotSec32, b.total_sectors32);
}

namespace {

void WriteVolumeInfo(uint8_t* info, const VolumeInfo& v) {
    info[kVolDrvNum]  = v.drive_number;
    info[kVolBootSig] = kExtBootSignature;
    cerf::le::Put32(info + kVolId, v.volume_id);
    std::memcpy(info + kVolLab, v.volume_label, 11);
    std::memcpy(info + kVolFilSysType, v.fs_type, 8);
}

}

void WriteFat16VolumeInfo(uint8_t* bs, const VolumeInfo& v) { WriteVolumeInfo(bs + kBs16VolumeInfo, v); }

void WriteFat32Extension(uint8_t* bs, const Fat32Extension& e) {
    cerf::le::Put32(bs + kBpb32FatSz32, e.fat_size32);
    cerf::le::Put16(bs + kBpb32ExtFlags, 0);
    cerf::le::Put16(bs + kBpb32FsVer, 0);
    cerf::le::Put32(bs + kBpb32RootClus, e.root_cluster);
    cerf::le::Put16(bs + kBpb32FsInfo, e.fsinfo_sector);
    cerf::le::Put16(bs + kBpb32BkBootSec, e.backup_boot_sector);
    WriteVolumeInfo(bs + kBs32VolumeInfo, e.volume);
}

uint32_t NewVolumeId() {
    const uint64_t us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                  std::chrono::system_clock::now().time_since_epoch())
                                                  .count());
    return static_cast<uint32_t>(us) ^ static_cast<uint32_t>(us >> 32);
}

void WriteFsInfo(uint8_t* fsi, uint32_t free_count, uint32_t next_free) {
    cerf::le::Put32(fsi + kFsiLeadSig, kFsiLeadSigValue);
    cerf::le::Put32(fsi + kFsiStrucSig, kFsiStrucSigValue);
    cerf::le::Put32(fsi + kFsiFreeCount, free_count);
    cerf::le::Put32(fsi + kFsiNxtFree, next_free);
    WriteBootSignature(fsi);
}

}

}
