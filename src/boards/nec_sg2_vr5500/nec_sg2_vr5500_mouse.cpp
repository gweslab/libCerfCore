#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/intel_i8042/i8042_controller.h"
#include "../../peripherals/relative_mouse/queued_relative_mouse_input.h"
#include <cerfcore/boards/board_context.h>
#include "nec_sg2_vr5500_family_id.h"

namespace cerfcore {

namespace {

/* kRelMouseLeft/Right (0x1/0x2) match Ps2Mouse::kButtonLeft/Right, so the button
   mask passes through unchanged. */
class NecSg2Vr5500Mouse : public QueuedRelativeMouseInput {
public:
    using QueuedRelativeMouseInput::QueuedRelativeMouseInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecSg2Vr5500;
    }

private:
    void OnMouseReady() override { i8042_ = &emu_.Get<I8042Controller>(); }

    void ApplyHostMotion(int dx, int dy, uint32_t button_mask) override {
        i8042_->HostMouseMotion(dx, dy, button_mask);
    }

    I8042Controller* i8042_ = nullptr;
};

}  /* namespace */

REGISTER_SERVICE_AS(NecSg2Vr5500Mouse, RelativeMouseInput);

}
