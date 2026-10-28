#include "../../peripherals/isa_debug_port/isa_debug_port_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_cronos3e_family_id.h"

namespace cerfcore {

namespace {

class NecCronos3eIsaDebugPortConfig : public IsaDebugPortConfig {
public:
    using IsaDebugPortConfig::IsaDebugPortConfig;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecCronos3e;
    }

    uint32_t IsaWindowBase() const override { return 0x1500FF00u; }
};

}

REGISTER_SERVICE_AS(NecCronos3eIsaDebugPortConfig, IsaDebugPortConfig);

}
