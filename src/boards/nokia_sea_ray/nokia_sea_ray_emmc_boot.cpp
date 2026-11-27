#include "../../boot/boot_mode.h"
#include "../../boot/kernel_image_copier.h"
#include "../../boot/rom_wmstore_parse.h"
#include "../../peripherals/mmc/emmc_card_base.h"
#include "../../peripherals/mmc/mmc_card.h"
#include "../page_table_builder.h"
#include "nokia_sea_ray_family_id.h"
#include "nokia_sea_ray_wmstore.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/hibernation.h>

#include <vector>

namespace cerfcore {

namespace {

namespace ri = cerf::rom_image_parse;

constexpr char kKernelPartition[] = "NK";

class NokiaSeaRayEmmcBoot : public BootMode {
public:
    using BootMode::BootMode;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override {
        BootMode::OnReady();
        PlaceKernel(false);
        emu_.Get<Hibernation>().RegisterRomImage(kernel_bytes_);
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { PlaceKernel(true); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { PlaceKernel(true); });
    }

    uint32_t ColdEntryPa() override { return KernelEntryPa(entry_va_); }

    bool ColdEntryThumb() override { return KernelEntryThumb(entry_va_); }

    uint32_t ColdStackPa() override {
        return emu_.Get<PageTableBuilder>().InitStackTopPa();
    }

private:
    EmmcCardBase& Card() { return static_cast<EmmcCardBase&>(emu_.Get<MmcCard>()); }

    std::vector<uint8_t> ReadSectors(uint64_t first, uint64_t count) {
        return Card().ReadStoredSectors(first, count);
    }

    ri::WmstorePartition KernelPartition() {
        ri::WmstorePartition nk;
        if (!emu_.Get<NokiaSeaRayWmstore>().FindPartition(kKernelPartition, nk))
            emu_.Get<Fatal>().Die("eMMC boot: the store has no %s partition", kKernelPartition);
        return nk;
    }

    uint64_t MemorySectors() {
        uint64_t bytes = 0;
        for (const auto& region : emu_.Get<PageTableBuilder>().BackedMemoryRegions())
            bytes += region.size;
        return bytes / cerf_mmc::kBlockBytes;
    }

    void PlaceKernel(bool replay) {
        const ri::WmstorePartition nk = KernelPartition();
        if (nk.size_lba > MemorySectors()) {
            emu_.Get<Fatal>().Die("eMMC boot: the %s partition of %llu sectors is larger than the "
                                  "memory of the board", kKernelPartition,
                                  static_cast<unsigned long long>(nk.size_lba));
        }
        const std::vector<uint8_t> image = ReadSectors(nk.start_lba, nk.size_lba);
        ri::WmstoreXip xip;
        if (!ri::WmstoreParseXip(image, xip)) {
            emu_.Get<Fatal>().Die("eMMC boot: the %s partition at sector %llu holds no XIP image",
                                  kKernelPartition, static_cast<unsigned long long>(nk.start_lba));
        }
        if (replay && xip.entry_va != entry_va_) {
            emu_.Get<Fatal>().Die("eMMC boot: the kernel entry moved from 0x%08X to 0x%08X, and a "
                                  "reset into a moved entry is not modeled", entry_va_, xip.entry_va);
        }
        emu_.Get<KernelImageCopier>().CopyToKernelVa(xip.base_va, image.data(), xip.flat_size,
                                                     replay, "eMMC boot");
        entry_va_     = xip.entry_va;
        kernel_bytes_ = xip.flat_size;
        LOG(Boot, "eMMC boot: %s partition at sector %llu, XIP 0x%08X, %u bytes, entry 0x%08X\n",
            kKernelPartition, static_cast<unsigned long long>(nk.start_lba), xip.base_va,
            xip.flat_size, entry_va_);
    }

    uint32_t entry_va_     = 0;
    uint32_t kernel_bytes_ = 0;
};

}

REGISTER_SERVICE_AS(NokiaSeaRayEmmcBoot, BootMode);

}
