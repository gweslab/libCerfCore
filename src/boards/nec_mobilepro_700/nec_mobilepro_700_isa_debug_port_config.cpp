#include "../../peripherals/isa_debug_port/isa_debug_port_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

namespace cerfcore {

namespace {

class NecMobilepro700IsaDebugPortConfig : public IsaDebugPortConfig {
public:
    using IsaDebugPortConfig::IsaDebugPortConfig;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    uint32_t IsaWindowBase() const override { return 0x1600FF00u; }
};

}

REGISTER_SERVICE_AS(NecMobilepro700IsaDebugPortConfig, IsaDebugPortConfig);

}
