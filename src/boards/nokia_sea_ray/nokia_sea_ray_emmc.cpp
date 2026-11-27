#include "../../peripherals/sk_hynix_h26m52002ckr/sk_hynix_h26m52002ckr.h"

#include "../../boot/rom_wmstore_parse.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/host_file_bytes.h>
#include <cerfcore/core/log.h>
#include <cerfcore/platform/host_file.h>
#include <cerfcore/state/state_stream.h>
#include "../../storage/block_read_overlay.h"
#include "../../storage/disk_image.h"
#include "../../storage/fat_volume_layout.h"
#include "nokia_sea_ray_emmc_layout.h"
#include <cerfcore/boards/board_context.h>
#include "nokia_sea_ray_family_id.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace cerfcore {

namespace {

namespace ri = cerf::rom_image_parse;

constexpr uint32_t kEmmcSlotIndex = 0u;

constexpr uint64_t kFlasherEraseUnit      = 0x800000u;
constexpr uint8_t  kFlasherWriteEraseFill = 0xFFu;
constexpr uint32_t kSeedChunkSectors      = 2048u;

constexpr uint32_t kMinStoreContainerOrigin = 2u;

constexpr uint32_t kHoleMarkerSector = 1u;
constexpr char     kHoleMarkerMagic[] = "CERF-UNMODELED:";
constexpr uint32_t kHoleMarkerMagicBytes = sizeof(kHoleMarkerMagic);
constexpr uint32_t kHoleMarkerEndOff = kHoleMarkerMagicBytes;
constexpr uint32_t kHoleMarkerBytes  = kHoleMarkerEndOff + 4u;
static_assert(kHoleMarkerMagicBytes == 16u);

class NokiaSeaRayEmmc : public SkHynixH26m52002ckr {
public:
    using SkHynixH26m52002ckr::SkHynixH26m52002ckr;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    uint32_t SlotIndex() const override { return kEmmcSlotIndex; }

    void OnReady() override {
        SkHynixH26m52002ckr::OnReady();
        OpenStore();
    }

    void OverlaySector(uint32_t sector, const uint8_t* bytes) override {
        if (sector >= SectorCount() || (sector >= kHoleMarkerSector && sector < hole_end_)) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: a read overlay of sector %u lies outside the sectors "
                "that storage.emmc %s models", kEmmcSlotIndex, sector, store_path_.c_str());
        }
        overlay_.Set(sector, bytes);
    }

    void SaveState(StateWriter& w) override {
        SkHynixH26m52002ckr::SaveState(w);
        if (emu_.Get<DeviceConfig>().guest_additions)
            overlay_.SaveState(w, "overlay_sector_count", "overlay_sector");
    }

    void RestoreState(StateReader& r) override {
        SkHynixH26m52002ckr::RestoreState(r);
        if (emu_.Get<DeviceConfig>().guest_additions)
            overlay_.RestoreState(r, "overlay_sector_count", "overlay_sector");
    }

protected:
    void ReadBlock(uint32_t sector, uint8_t* out) override {
        if (const uint8_t* overlay = overlay_.Find(sector)) {
            std::memcpy(out, overlay, cerf_mmc::kBlockBytes);
            return;
        }
        if (sector >= kHoleMarkerSector && sector < hole_end_) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: sector %u lies in [%u, %u), which the seed of "
                "storage.emmc %s did not model", kEmmcSlotIndex, sector,
                kHoleMarkerSector, hole_end_, store_path_.c_str());
        }
        if (!store_.ReadSectors(sector, 1u, out)) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: sector %u cannot be read from %s",
                kEmmcSlotIndex, sector, store_path_.c_str());
        }
    }

    void WriteBlock(uint32_t sector, const uint8_t* data) override {
        if (sector >= kHoleMarkerSector && sector < hole_end_) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: a write to sector %u lands in [%u, %u), "
                "which the seed of storage.emmc %s marks as not modeled, and a "
                "write there is not modeled", kEmmcSlotIndex, sector,
                kHoleMarkerSector, hole_end_, store_path_.c_str());
        }
        overlay_.Drop(sector);
        if (!store_.WriteSectors(sector, 1u, data)) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: sector %u cannot be written to %s",
                kEmmcSlotIndex, sector, store_path_.c_str());
        }
    }

private:
    void LoadPackage() {
        const DeviceConfig& cfg = emu_.Get<DeviceConfig>();
        package_name_ = cfg.rom_primary;
        if (package_name_.empty()) {
            emu_.Get<Fatal>().UserError(
                "eMMC card in slot %u: storage.emmc %s does not exist and rom.primary "
                "is not set, so the card cannot be seeded", kEmmcSlotIndex,
                store_path_.c_str());
        }
        if (!TryReadHostFileBytes(ResolveDeviceFile(cfg.device_name, package_name_),
                                  package_)) {
            emu_.Get<Fatal>().UserError("eMMC card in slot %u: rom.primary %s cannot be read",
                                        kEmmcSlotIndex, package_name_.c_str());
        }
        std::vector<ri::EscoMember> members;
        if (!ri::EscoStoredMembers(package_, members)) {
            emu_.Get<Fatal>().UserError(
                "eMMC card in slot %u: rom.primary %s is not a firmware package",
                kEmmcSlotIndex, package_name_.c_str());
        }
        payload_ = members[0];
    }

    void LoadImageOrigin() {
        ri::EscoRange image;
        if (!ri::EscoImageRange(package_, image)) {
            emu_.Get<Fatal>().UserError(
                "eMMC card in slot %u: rom.primary %s carries no image write record, so "
                "its place on the card is not known", kEmmcSlotIndex,
                package_name_.c_str());
        }
        if (image.start % cerf_mmc::kBlockBytes != 0u) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: %s is written at card byte 0x%llX, which "
                "is not a whole sector", kEmmcSlotIndex, package_name_.c_str(),
                static_cast<unsigned long long>(image.start));
        }
        const uint64_t origin = image.start / cerf_mmc::kBlockBytes;
        const uint64_t payload_sectors =
            (uint64_t(payload_.bytes) + cerf_mmc::kBlockBytes - 1u) /
            cerf_mmc::kBlockBytes;
        if (origin + payload_sectors > SectorCount()) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: %s ends at sector %llu, past the %u sectors "
                "of the card", kEmmcSlotIndex, package_name_.c_str(),
                static_cast<unsigned long long>(origin + payload_sectors),
                SectorCount());
        }
        if (origin < kMinStoreContainerOrigin) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: %s is written at sector %llu, which leaves "
                "no room for the partition table ahead of it", kEmmcSlotIndex,
                package_name_.c_str(), static_cast<unsigned long long>(origin));
        }
        image_target_  = image.target;
        image_drive_   = image.drive;
        image_origin_  = static_cast<uint32_t>(origin);
        image_end_     = static_cast<uint32_t>(origin + payload_sectors);
        LOG(Boot, "eMMC card in slot %u: %s is written at part sector %u\n",
            kEmmcSlotIndex, package_name_.c_str(), image_origin_);
    }

    void OpenStore() {
        const DeviceConfig& cfg = emu_.Get<DeviceConfig>();
        store_path_ = ResolveDeviceFile(cfg.device_name, cfg.storage_emmc);
        if (HostFileNonEmpty(store_path_)) {
            LOG(Boot, "eMMC card in slot %u: using storage.emmc %s\n",
                kEmmcSlotIndex, store_path_.c_str());
        } else {
            SeedStore();
        }
        OpenCardImage(store_, store_path_);
        LoadHoleMarker();
    }

    void LoadHoleMarker() {
        uint8_t sector[cerf_mmc::kBlockBytes];
        if (!store_.ReadSectors(kHoleMarkerSector, 1u, sector)) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: sector %u cannot be read from storage.emmc %s",
                kEmmcSlotIndex, kHoleMarkerSector, store_path_.c_str());
        }
        if (std::memcmp(sector, kHoleMarkerMagic, kHoleMarkerMagicBytes) != 0) return;
        const uint32_t end = cerf::le::U32(sector, kHoleMarkerEndOff);
        const bool tail_clear = std::all_of(sector + kHoleMarkerBytes, std::end(sector),
                                            [](uint8_t b) { return b == 0u; });
        if (end <= kHoleMarkerSector || end > SectorCount() || !tail_clear) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: sector %u of storage.emmc %s starts with the "
                "seed marker, and the rest of it is not a marker for the %u sectors "
                "of the card", kEmmcSlotIndex, kHoleMarkerSector, store_path_.c_str(),
                SectorCount());
        }
        hole_end_ = end;
        LOG(Boot, "eMMC card in slot %u: storage.emmc %s marks sectors [%u, %u) as "
                  "not modeled by its seed\n", kEmmcSlotIndex, store_path_.c_str(),
            kHoleMarkerSector, hole_end_);
    }

    void OpenCardImage(DiskImage& image, const std::string& path) {
        if (!image.Open(path, uint64_t(SectorCount()) * cerf_mmc::kBlockBytes)) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: storage.emmc %s cannot be opened", kEmmcSlotIndex, path.c_str());
        }
        if (image.SectorCount() != SectorCount()) {
            emu_.Get<Fatal>().UserError(
                "eMMC card in slot %u: storage.emmc %s holds %llu sectors, and the "
                "card has %u", kEmmcSlotIndex, path.c_str(),
                static_cast<unsigned long long>(image.SectorCount()), SectorCount());
        }
    }

    void SeedStore() {
        const std::string seeding = store_path_ + ".seeding";
        LOG(Boot, "eMMC card in slot %u: seeding storage.emmc %s\n",
            kEmmcSlotIndex, store_path_.c_str());
        LoadPackage();
        LoadImageOrigin();
        LoadUserAreaErase();
        uint32_t err = 0;
        if (host_platform::DeleteHostFile(seeding, err) == host_platform::DeleteResult::kFailed) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: %s is left from an earlier seed and "
                "cannot be deleted (error %lu)", kEmmcSlotIndex, seeding.c_str(),
                static_cast<unsigned long>(err));
        }
        {
            DiskImage image;
            OpenCardImage(image, seeding);
            SeedStoreContainer(image);
            SeedHoleMarker(image);
            SeedPayload(image);
            SeedErase(image);
        }
        std::vector<uint8_t>().swap(package_);
        if (!host_platform::ReplaceHostFile(seeding, store_path_, false)) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: the seeded card image %s cannot be moved to "
                "storage.emmc %s", kEmmcSlotIndex, seeding.c_str(),
                store_path_.c_str());
        }
        LOG(Boot, "eMMC card in slot %u: storage.emmc %s seeded\n",
            kEmmcSlotIndex, store_path_.c_str());
    }

    void LoadUserAreaErase() {
        const DeviceConfig& cfg = emu_.Get<DeviceConfig>();
        const std::string& name = cfg.rom_lumia800_user_area_erase;
        if (name.empty()) {
            emu_.Get<Fatal>().UserError(
                "eMMC card in slot %u: storage.emmc %s does not exist and "
                "rom.lumia800_user_area_erase is not set, so the card cannot be seeded",
                kEmmcSlotIndex, store_path_.c_str());
        }
        std::vector<uint8_t> bytes;
        if (!TryReadHostFileBytes(ResolveDeviceFile(cfg.device_name, name), bytes)) {
            emu_.Get<Fatal>().UserError("eMMC card in slot %u: rom.lumia800_user_area_erase %s cannot be read",
                                        kEmmcSlotIndex, name.c_str());
        }
        ri::EscoRange erase;
        if (!ri::EscoEraseRange(bytes, erase)) {
            emu_.Get<Fatal>().UserError(
                "eMMC card in slot %u: rom.lumia800_user_area_erase %s is not an "
                "erase package", kEmmcSlotIndex, name.c_str());
        }
        if (erase.target != image_target_ || erase.drive != image_drive_) {
            emu_.Get<Fatal>().UserError(
                "eMMC card in slot %u: rom.lumia800_user_area_erase %s names "
                "target %u, drive %u, but %s is written to target %u, drive %u",
                kEmmcSlotIndex, name.c_str(), erase.target, erase.drive,
                package_name_.c_str(), image_target_, image_drive_);
        }
        if (erase.start % cerf_mmc::kBlockBytes != 0u ||
            erase.size % cerf_mmc::kBlockBytes != 0u) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: rom.lumia800_user_area_erase %s erases "
                "0x%llX bytes at 0x%llX, which is not whole sectors",
                kEmmcSlotIndex, name.c_str(),
                static_cast<unsigned long long>(erase.size),
                static_cast<unsigned long long>(erase.start));
        }
        const uint64_t start = erase.start / cerf_mmc::kBlockBytes;
        const uint64_t count = erase.size / cerf_mmc::kBlockBytes;
        if (start < image_end_) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: rom.lumia800_user_area_erase %s starts "
                "before the end of the %zu payload bytes of %s, and only an erase "
                "range past the payload is modeled", kEmmcSlotIndex, name.c_str(),
                payload_.bytes, package_name_.c_str());
        }
        if (start >= SectorCount()) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: rom.lumia800_user_area_erase %s starts at "
                "sector %llu, past the %u sectors of the card", kEmmcSlotIndex,
                name.c_str(), static_cast<unsigned long long>(start),
                SectorCount());
        }
        const uint64_t end  = std::min<uint64_t>(start + count, SectorCount());
        const uint64_t unit = kFlasherEraseUnit / cerf_mmc::kBlockBytes;
        const uint64_t hw_start =
            std::min<uint64_t>((start + unit - 1u) / unit * unit, end);
        const uint64_t hw_end = std::max<uint64_t>(end / unit * unit, hw_start);
        erase_start_    = static_cast<uint32_t>(start);
        erase_end_      = static_cast<uint32_t>(end);
        hw_erase_start_ = static_cast<uint32_t>(hw_start);
        hw_erase_end_   = static_cast<uint32_t>(hw_end);
        LOG(Boot, "eMMC card in slot %u: %s erases sectors [%u, %u), hardware "
                  "erase [%u, %u)\n", kEmmcSlotIndex, name.c_str(), erase_start_,
            erase_end_, hw_erase_start_, hw_erase_end_);
    }

    void SeedStoreContainer(DiskImage& image) {
        uint8_t mbr[cerf_mmc::kBlockBytes] = {};
        fat_volume_layout::MbrPartition extended;
        extended.type      = kNokiaSeaRayPartitionExtended;
        extended.start_lba = image_origin_ - kNokiaSeaRayEbrStoreRelativeLba;
        fat_volume_layout::WriteMbrPartition(mbr, 0u, extended);
        fat_volume_layout::WriteBootSignature(mbr);
        WriteSeed(image, 0u, 1u, mbr);

        uint8_t ebr[cerf_mmc::kBlockBytes] = {};
        fat_volume_layout::MbrPartition store;
        store.type      = kNokiaSeaRayPartitionStore;
        store.start_lba = kNokiaSeaRayEbrStoreRelativeLba;
        fat_volume_layout::WriteMbrPartition(ebr, 0u, store);
        fat_volume_layout::WriteBootSignature(ebr);
        WriteSeed(image, extended.start_lba, 1u, ebr);
    }

    void SeedHoleMarker(DiskImage& image) {
        const uint32_t end = image_origin_ - 1u;
        if (end <= kHoleMarkerSector) return;
        uint8_t marker[cerf_mmc::kBlockBytes] = {};
        std::memcpy(marker, kHoleMarkerMagic, kHoleMarkerMagicBytes);
        cerf::le::Put32(marker + kHoleMarkerEndOff, end);
        WriteSeed(image, kHoleMarkerSector, 1u, marker);
    }

    void SeedPayload(DiskImage& image) {
        if (payload_.bytes % cerf_mmc::kBlockBytes != 0u) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: the %zu payload bytes of %s end inside a "
                "sector, and the rest of that sector is not modeled",
                kEmmcSlotIndex, payload_.bytes, package_name_.c_str());
        }
        const uint8_t* payload = package_.data() + payload_.off;
        const uint32_t sectors =
            static_cast<uint32_t>(payload_.bytes / cerf_mmc::kBlockBytes);
        for (uint32_t done = 0; done < sectors;) {
            const uint32_t n = (std::min)(kSeedChunkSectors, sectors - done);
            WriteSeed(image, image_origin_ + done, n,
                      payload + size_t(done) * cerf_mmc::kBlockBytes);
            done += n;
        }
    }

    void SeedErase(DiskImage& image) {
        FillSeed(image, erase_start_, hw_erase_start_, kFlasherWriteEraseFill);
        FillSeed(image, hw_erase_start_, hw_erase_end_, ErasedByte());
        FillSeed(image, hw_erase_end_, erase_end_, kFlasherWriteEraseFill);
    }

    void FillSeed(DiskImage& image, uint32_t first, uint32_t end, uint8_t fill) {
        if (first >= end || fill == 0u) return;
        const std::vector<uint8_t> chunk(size_t(kSeedChunkSectors) * cerf_mmc::kBlockBytes,
                                         fill);
        for (uint32_t at = first; at < end;) {
            const uint32_t n = (std::min)(kSeedChunkSectors, end - at);
            WriteSeed(image, at, n, chunk.data());
            at += n;
        }
    }

    void WriteSeed(DiskImage& image, uint32_t sector, uint32_t count,
                   const void* src) {
        if (!image.WriteSectors(sector, count, src)) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: the card image for storage.emmc %s cannot be "
                "written at sector %u", kEmmcSlotIndex, store_path_.c_str(), sector);
        }
    }

    DiskImage        store_;
    BlockReadOverlay overlay_{cerf_mmc::kBlockBytes};
    std::string      store_path_;
    std::string          package_name_;
    std::vector<uint8_t> package_;
    ri::EscoMember       payload_;
    uint32_t hole_end_       = 0u;
    uint32_t image_target_   = 0u;
    uint32_t image_drive_    = 0u;
    uint32_t image_origin_   = 0u;
    uint32_t image_end_      = 0u;
    uint32_t erase_start_    = 0u;
    uint32_t erase_end_      = 0u;
    uint32_t hw_erase_start_ = 0u;
    uint32_t hw_erase_end_   = 0u;
};

}  // namespace

REGISTER_SERVICE_AS(NokiaSeaRayEmmc, MmcCard);

}
