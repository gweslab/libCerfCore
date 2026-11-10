#include "../../peripherals/hp_debug_board/hp_debug_board_placement.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sh7729/sh7729_id.h"
#include "hp_600_family_id.h"

namespace cerfcore {

namespace {

class Hp600Sh7729DebugBoardPlacement : public HpDebugBoardPlacement {
public:
    using HpDebugBoardPlacement::HpDebugBoardPlacement;

    bool ShouldRegister() override {
        const BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7729;
    }

    uint32_t BasePa() const override { return 0x03000000u; }
};

}

REGISTER_SERVICE_AS(Hp600Sh7729DebugBoardPlacement, HpDebugBoardPlacement);

}
