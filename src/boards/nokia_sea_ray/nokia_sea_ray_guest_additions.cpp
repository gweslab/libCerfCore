#include "../../boot/imgfs_stub_injector.h"
#include "../../boot/imgfs_volume_access.h"
#include "../../peripherals/mmc/emmc_card_base.h"
#include "../../peripherals/mmc/mmc_card.h"
#include "nokia_sea_ray_family_id.h"
#include "nokia_sea_ray_wmstore.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/service.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace {

constexpr char kImgfsPartition[] = "IMGFS";
constexpr char kCantRun[]        = "Guest Additions can't run on this ROM";

class NokiaSeaRayGuestAdditions : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay &&
               emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        cerf::rom_image_parse::WmstorePartition imgfs;
        if (!emu_.Get<NokiaSeaRayWmstore>().FindPartition(kImgfsPartition, imgfs)) {
            LOG(GuestAdditions, "the eMMC store has no %s partition\n", kImgfsPartition);
            emu_.Get<Fatal>().UserError(kCantRun);
        }
        Volume volume(*this, imgfs.start_lba, imgfs.size_lba);
        const int replaced = emu_.Get<ImgfsStubInjector>().ReplaceVictims(volume);
        LOG(GuestAdditions, "eMMC %s partition at sector %llu, %llu sectors: %d victim(s) "
                            "replaced\n", kImgfsPartition,
            static_cast<unsigned long long>(imgfs.start_lba),
            static_cast<unsigned long long>(imgfs.size_lba), replaced);
        if (replaced == 0) {
            LOG(GuestAdditions, "no display-driver victim matched\n");
            emu_.Get<Fatal>().UserError(kCantRun);
        }
    }

private:
    class Volume final : public ImgfsVolumeAccess {
    public:
        Volume(NokiaSeaRayGuestAdditions& owner, uint64_t first, uint64_t sectors)
            : owner_(owner), first_(first), bytes_(sectors * cerf_mmc::kBlockBytes) {}

        uint32_t SectorBytes() const override { return cerf_mmc::kBlockBytes; }

        std::vector<uint8_t> Read(uint32_t offset, uint32_t length) override {
            const uint64_t end = uint64_t(offset) + length;
            if (length == 0u || end > bytes_) return {};
            const uint64_t first = offset / cerf_mmc::kBlockBytes;
            const uint64_t last  = (end + cerf_mmc::kBlockBytes - 1u) / cerf_mmc::kBlockBytes;
            const std::vector<uint8_t> sectors =
                owner_.Card().ReadStoredSectors(first_ + first, last - first);
            const size_t in = offset % cerf_mmc::kBlockBytes;
            return {sectors.begin() + in, sectors.begin() + in + length};
        }

        void Patch(uint32_t offset, std::span<const uint8_t> bytes) override {
            if (uint64_t(offset) + bytes.size() > bytes_) {
                owner_.emu_.Get<Fatal>().Die(
                    "eMMC %s partition: a %zu-byte patch at 0x%X ends past its 0x%llX bytes",
                    kImgfsPartition, bytes.size(), offset,
                    static_cast<unsigned long long>(bytes_));
            }
            EmmcCardBase& card = owner_.Card();
            for (size_t done = 0; done < bytes.size();) {
                const uint64_t at     = uint64_t(offset) + done;
                const uint32_t in     = uint32_t(at % cerf_mmc::kBlockBytes);
                const size_t   n      = (std::min)(size_t(cerf_mmc::kBlockBytes - in), bytes.size() - done);
                const uint64_t sector = first_ + at / cerf_mmc::kBlockBytes;
                std::vector<uint8_t> block = card.ReadStoredSectors(sector, 1u);
                std::memcpy(block.data() + in, bytes.data() + done, n);
                card.OverlaySector(uint32_t(sector), block.data());
                done += n;
            }
        }

    private:
        NokiaSeaRayGuestAdditions& owner_;
        uint64_t                   first_;
        uint64_t                   bytes_;
    };

    EmmcCardBase& Card() { return static_cast<EmmcCardBase&>(emu_.Get<MmcCard>()); }
};

}

REGISTER_SERVICE(NokiaSeaRayGuestAdditions);

}
