#include "siemens_simatic_mobile_panels_gen2_card_store.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/platform/host_file.h>
#include <cerfcore/core/host_file_bytes.h>
#include "../../net/network_backend.h"
#include "../../socs/imx6/imx6_fec.h"
#include "siemens_simatic_mobile_panels_gen2_board_profile.h"
#include "siemens_simatic_mobile_panels_gen2_factory_layout.h"
#include "siemens_simatic_mobile_panels_gen2_flashboot.h"
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kBlockBytes  = 512u;
constexpr uint32_t kSectorCount = siemens_simatic_mobile_panels_gen2_emmc::kCardSectors;

}

bool SiemensSimaticMobilePanelsGen2CardStore::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
}

void SiemensSimaticMobilePanelsGen2CardStore::OnReady() {
    const auto& config = emu_.Get<DeviceConfig>();
    const auto& profile = SiemensSimaticMobilePanelsGen2BoardProfileFor(emu_.Get<BoardContext>().GetBoardId());
    siemens_simatic_mobile_panels_gen2_emmc::CardInputs inputs;
    const std::string fwf_path = GetDeviceDir(config.device_name) + config.rom_primary;
    inputs.hardware_mac  = emu_.Get<NetworkBackend>().MacForReceiver(kImx6FecReceiverId, NetworkBackend::ReceiverKind::Ethernet);
    inputs.op_type       = profile.op_type;
    inputs.panel         = ResolveSiemensSimaticMobilePanelsGen2Panel(emu_.Get<BoardDatabase>().FindBoard(profile.board), profile);

    path_ = ResolveDeviceFile(config.device_name, config.storage_emmc);
    if (HostFileNonEmpty(path_)) {
        DiskImage probe;
        if (!probe.Open(path_, uint64_t(kSectorCount) * kBlockBytes, false))
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: storage.emmc %s cannot be opened", path_.c_str());
        if (!siemens_simatic_mobile_panels_gen2_emmc::CardLayoutValid(probe))
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: storage.emmc %s does not hold the KTP card layout; it is left "
                                  "untouched, move it away to have a new card seeded",
                                  path_.c_str());
    } else {
        if (config.rom_primary.empty() || !TryReadHostFileBytes(fwf_path, inputs.fwf_container))
            LOG(Caution, "Mobile Panels 2nd Gen eMMC: %s absent or unreadable; /flashboot cannot be programmed\n", fwf_path.c_str());
        SeedCardFile(inputs);
    }
    if (!card_.Open(path_, uint64_t(kSectorCount) * kBlockBytes))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: storage.emmc %s cannot be opened", path_.c_str());
    if (!siemens_simatic_mobile_panels_gen2_emmc::RefreshFactoryArea(card_, inputs))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: the factory area of %s cannot be written", path_.c_str());
    std::string error;
    if (!siemens_simatic_mobile_panels_gen2_flashboot::HasBootRecipe(card_, error))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: /flashboot of %s holds no bootable image (%s); the card is left "
                              "untouched, move it away to have a new card seeded",
                              path_.c_str(), error.c_str());
}

void SiemensSimaticMobilePanelsGen2CardStore::SeedCardFile(const siemens_simatic_mobile_panels_gen2_emmc::CardInputs& inputs) {
    const std::string seeding = path_ + ".seeding";
    LOG(Boot, "Mobile Panels 2nd Gen eMMC: seeding storage.emmc %s\n", path_.c_str());
    uint32_t err = 0;
    if (host_platform::DeleteHostFile(seeding, err) == host_platform::DeleteResult::kFailed)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: %s is left from an earlier seed and cannot be deleted (error %lu)",
                              seeding.c_str(), static_cast<unsigned long>(err));
    {
        DiskImage image;
        if (!image.Open(seeding, uint64_t(kSectorCount) * kBlockBytes))
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: %s cannot be created", seeding.c_str());
        std::string error;
        if (!siemens_simatic_mobile_panels_gen2_emmc::SeedCard(image, inputs, error))
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: %s cannot be seeded: %s", seeding.c_str(), error.c_str());
        if (!siemens_simatic_mobile_panels_gen2_flashboot::ProgramFromFwf(image, inputs.fwf_container, error))
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: /flashboot of %s cannot be programmed: %s", seeding.c_str(),
                                  error.c_str());
    }
    if (!host_platform::ReplaceHostFile(seeding, path_, false))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: the seeded card image %s cannot be moved to storage.emmc %s",
                              seeding.c_str(), path_.c_str());
}

void SiemensSimaticMobilePanelsGen2CardStore::ReadSector(uint32_t sector, uint8_t* out) {
    RequireSector(sector, "read");
    if (!card_.ReadSectors(sector, 1u, out))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: sector %u cannot be read from %s", sector, path_.c_str());
}

void SiemensSimaticMobilePanelsGen2CardStore::WriteSector(uint32_t sector, const uint8_t* data) {
    RequireSector(sector, "write");
    if (!card_.WriteSectors(sector, 1u, data))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: sector %u cannot be written to %s", sector, path_.c_str());
}

void SiemensSimaticMobilePanelsGen2CardStore::RequireSector(uint32_t sector, const char* what) const {
    if (sector >= kSectorCount)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen eMMC: %s of sector %u past the %u-sector card", what, sector, kSectorCount);
}

REGISTER_SERVICE(SiemensSimaticMobilePanelsGen2CardStore);

}
