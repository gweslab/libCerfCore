#include "../../cpu/sh/sh_board_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "hp_600_family_id.h"

namespace cerfcore {

namespace {

class Hp600Sh7709ShBoardClock : public ShBoardClock {
public:
    using ShBoardClock::ShBoardClock;

    bool ShouldRegister() override {
        const BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    uint64_t CkioHz() const override { return 18432000u; }
    bool     Pll1Switchable() const override { return true; }
};

}

REGISTER_SERVICE_AS(Hp600Sh7709ShBoardClock, ShBoardClock);

}
