#include "../../boot/board_boot_placer.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "philips_velo_1_family_id.h"
#include "philips_velo_1_internal_flash.h"
#include "philips_velo_1_rom_upgrade_card.h"

#include <cstdint>
#include <span>

namespace cerfcore {

namespace {

class PhilipsVelo1BootPlacer : public BoardBootPlacer {
public:
    using BoardBootPlacer::BoardBootPlacer;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
    }

    void PlaceAfterRom() override {
        auto& mem = emu_.Get<EmulatedMemory>();

        const auto& flash = emu_.Get<PhilipsVelo1InternalFlash>();
        const std::span<const uint8_t> mirror = flash.ResetMirrorBytes();
        if (!mirror.empty()) {
            mem.LoadContents(flash.ResetMirrorPa(), mirror.data(), mirror.size());
            LOG(Boot, "PhilipsVelo1BootPlacer: internal flash mirror 0x%zX bytes at pa=0x%08X\n",
                mirror.size(), flash.ResetMirrorPa());
        }

        const auto& card = emu_.Get<PhilipsVelo1RomUpgradeCard>();
        if (!card.Fitted()) return;
        for (const Velo1CardWindow& w : { card.KsegWindow(), card.KusegWindow() }) {
            mem.LoadContents(w.pa, w.bytes.data(), w.bytes.size());
            LOG(Boot, "PhilipsVelo1BootPlacer: ROM upgrade card 0x%zX bytes at pa=0x%08X\n",
                w.bytes.size(), w.pa);
        }
    }
};

}

REGISTER_SERVICE_AS(PhilipsVelo1BootPlacer, BoardBootPlacer);

}
