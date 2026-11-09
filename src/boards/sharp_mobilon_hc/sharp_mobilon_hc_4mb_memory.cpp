#include "sharp_mobilon_hc_memory.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../core/board_config.h"
#include "../ram_param_id.h"
#include "sharp_mobilon_hc_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

class SharpMobilonHc4mbMemory : public SharpMobilonHcMemory {
public:
    using SharpMobilonHcMemory::SharpMobilonHcMemory;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc &&
               emu_.Get<BoardConfig>().EnumId(BoardConfigParamId::Ram) == "4mb";
    }

    uint32_t MemConfig0Banks() const override { return 0x00011090u; }
    uint32_t Bank0Bytes() const override { return 0x00400000u; }
    uint32_t Bank1Bytes() const override { return 0u; }
    bool     Cs1RamTag() const override { return false; }
};

}

REGISTER_SERVICE_AS(SharpMobilonHc4mbMemory, SharpMobilonHcMemory);

}
