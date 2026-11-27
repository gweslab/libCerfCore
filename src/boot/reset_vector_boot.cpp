#include "boot_mode.h"
#include "reset_vector_placing_mode_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class ResetVectorBoot : public BootMode {
public:
    using BootMode::BootMode;

    bool ShouldRegister() override {
        const std::string_view mode = emu_.Get<BoardContext>().GetRomPlacingMode();
        return mode == RomPlacingModeId::ResetVector || mode.empty();
    }

    uint32_t ColdEntryPa() override { return 0; }
    uint32_t ColdStackPa() override { return 0; }
};

}

REGISTER_SERVICE_AS(ResetVectorBoot, BootMode);

}
