#include "../../boot/board_boot_image.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "siemens_simatic_mobile_panels_gen2_card_store.h"
#include "siemens_simatic_mobile_panels_gen2_emmc_store.h"
#include "siemens_simatic_mobile_panels_gen2_factory_layout.h"
#include "siemens_simatic_mobile_panels_gen2_flashboot.h"
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kBootStateReset = 0x96A50002u;

class SiemensSimaticMobilePanelsGen2BootLoader : public BoardBootImage {
public:
    using BoardBootImage::BoardBootImage;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }

    void OnReady() override {
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reboot();
        });
    }

    bool ReadBootImage(std::vector<uint8_t>& flat, std::string& error) override {
        DiskImage& card = emu_.Get<SiemensSimaticMobilePanelsGen2CardStore>().Card();
        RequireImageBootState(card);
        siemens_simatic_mobile_panels_gen2_flashboot::BootImage image;
        if (!siemens_simatic_mobile_panels_gen2_flashboot::ReadBootImage(card, image, error)) return false;
        LOG(Boot, "SiemensSimaticMobilePanelsGen2BootLoader: /flashboot image %s, %zu bytes at VA 0x%08X, entry 0x%08X\n",
            image.version.c_str(), image.flat.size(), image.base_va, image.entry_va);
        flat = std::move(image.flat);
        loaded_stream_crc_ = image.stream_crc;
        return true;
    }

private:
    void RequireImageBootState(DiskImage& card) {
        std::vector<uint8_t> word;
        if (!siemens_simatic_mobile_panels_gen2_emmc::ReadCardRange(card, siemens_simatic_mobile_panels_gen2_emmc::kBootStateOff, 4u, word))
            emu_.Get<Fatal>().Die("SiemensSimaticMobilePanelsGen2BootLoader: the boot state cannot be read");
        const uint32_t state = cerf::le::U32(word.data());
        if (state != kBootStateReset) return;
        emu_.Get<Fatal>().Die("SiemensSimaticMobilePanelsGen2BootLoader: boot state BS_RESET (Prepare for Reset) asks the boot loader "
                              "to download the OS and reset to factory settings; that mode is not modelled");
    }

    void Reboot() {
        DiskImage& card = emu_.Get<SiemensSimaticMobilePanelsGen2CardStore>().Card();
        RequireImageBootState(card);
        uint32_t stream_crc = 0;
        std::string error;
        if (!siemens_simatic_mobile_panels_gen2_flashboot::VerifyBootImage(card, stream_crc, error))
            emu_.Get<Fatal>().Die("SiemensSimaticMobilePanelsGen2BootLoader: no bootable image in /flashboot at reset: %s",
                                  error.c_str());
        if (stream_crc != loaded_stream_crc_) {
            auto& parser = emu_.Get<RomParserService>();
            const uint32_t current_entry_va = parser.Primary().entry_va;
            parser.ReleasePrimaryImage();
            ParsedRom next;
            if (!parser.ReparsePrimary(next))
                emu_.Get<Fatal>().Die("SiemensSimaticMobilePanelsGen2BootLoader: no bootable image in /flashboot at reset");
            if (next.entry_va != current_entry_va)
                emu_.Get<Fatal>().Die("SiemensSimaticMobilePanelsGen2BootLoader: the /flashboot image changed at reset and moves the "
                                      "kernel entry point (0x%08X -> 0x%08X); rebuilding it is not modelled",
                                      current_entry_va, next.entry_va);
            parser.ReplacePrimary(std::move(next));
        }
        auto& cold_boot = emu_.Get<GuestColdBoot>();
        if (!cold_boot.Pending()) cold_boot.ReplayBootWrites();
    }

    uint32_t loaded_stream_crc_ = 0;
};

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2BootLoader, BoardBootImage);

}
