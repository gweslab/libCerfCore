#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/touch_input.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "hp_600_family_id.h"
#include "hp_600_sh7709_touch_panel.h"

namespace cerfcore {

namespace {

class Hp600Sh7709TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    void OnReady() override { panel_ = &emu_.Get<Hp600Sh7709TouchPanel>(); }

    void OnPenDown(int x, int y) override { panel_->PenDown(x, y); }
    void OnPenMove(int x, int y) override { panel_->PenMove(x, y); }
    void OnPenUp(int, int) override { panel_->PenUp(); }
    void OnCaptureLost() override { panel_->PenUp(); }

private:
    Hp600Sh7709TouchPanel* panel_ = nullptr;
};

}

REGISTER_SERVICE_AS(Hp600Sh7709TouchInput, TouchInput);

}
