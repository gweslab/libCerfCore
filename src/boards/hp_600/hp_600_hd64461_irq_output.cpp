#include "../../peripherals/hitachi_hd64461/hd64461_irq_output.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sh77xx/sh77xx_intc.h"
#include "hp_600_family_id.h"

namespace cerfcore {

namespace {

class Hp600Hd64461IrqOutput : public Hd64461IrqOutput {
public:
    using Hd64461IrqOutput::Hd64461IrqOutput;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
    }

    void OnReady() override { intc_ = &emu_.Get<Sh77xxIntc>(); }

    void SetIrq0Asserted(bool asserted) override { intc_->SetPinLevel(Sh77xxIntcPin::kIrq4, !asserted); }

private:
    Sh77xxIntc* intc_ = nullptr;
};

}

REGISTER_SERVICE_AS(Hp600Hd64461IrqOutput, Hd64461IrqOutput);

}
