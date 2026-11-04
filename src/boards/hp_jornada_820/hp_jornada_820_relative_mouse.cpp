#include "../../peripherals/relative_mouse/queued_relative_mouse_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_820_family_id.h"
#include "hp_jornada_820_companion_asic.h"

namespace cerfcore {

namespace {

class HpJornada820RelativeMouse : public QueuedRelativeMouseInput {
public:
    using QueuedRelativeMouseInput::QueuedRelativeMouseInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada820;
    }

    std::string SourceName() const override { return "Stock PS/2 mouse"; }

private:
    void OnMouseReady() override { asic_ = &emu_.Get<HpJornada820CompanionAsic>(); }

    void ApplyHostMotion(int dx, int dy, uint32_t button_mask) override {
        asic_->QueuePs2Motion(dx, dy, button_mask);
    }

    HpJornada820CompanionAsic* asic_ = nullptr;
};

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada820RelativeMouse, RelativeMouseInput);

}
