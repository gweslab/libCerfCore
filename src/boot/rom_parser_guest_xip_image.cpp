#include "guest_xip_image.h"

#include "rom_placer.h"
#include <cerfcore/boards/board_context.h>
#include "flat_container_placing_mode_id.h"
#include <cerfcore/boot/rom_parser_queries.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>

namespace cerfcore {

namespace {

class RomParserGuestXipImage final : public GuestXipImage {
public:
    using GuestXipImage::GuestXipImage;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions &&
               emu_.Get<BoardContext>().GetRomPlacingMode() == RomPlacingModeId::FlatContainer &&
               emu_.Get<RomParserService>().Ok();
    }

    const ParsedXipRegion* Primary() override { return emu_.Get<RomPlacer>().PlacedPrimaryXip(); }

    const ParsedTOCentry* FindModule(const char* name) override {
        return emu_.Get<RomParserQueries>().FindModule(name);
    }

    bool KernelSubsystemVersion(uint16_t& major, uint16_t& minor) override {
        return emu_.Get<RomParserQueries>().KernelSubsystemVersion(major, minor);
    }

    void Patch(uint32_t pa, std::span<const uint8_t> bytes) override { PatchRam(pa, bytes); }
};

}

REGISTER_SERVICE_AS(RomParserGuestXipImage, GuestXipImage);

}
