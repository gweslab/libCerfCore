#include "../../peripherals/hp_debug_board/hp_debug_board_placement.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "hp_jornada_700_family_id.h"

namespace cerfcore {

namespace {

class HpJornada700DebugBoardPlacement : public HpDebugBoardPlacement {
public:
    using HpDebugBoardPlacement::HpDebugBoardPlacement;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    uint32_t BasePa() const override { return 0x1A000000u; }
};

}

REGISTER_SERVICE_AS(HpJornada700DebugBoardPlacement, HpDebugBoardPlacement);

}
