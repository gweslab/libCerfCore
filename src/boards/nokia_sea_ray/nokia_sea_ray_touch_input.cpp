#include "../../core/board_parts.h"
#include "../../peripherals/atmel_mxt224e/atmel_mxt224e_part_id.h"
#include "../../peripherals/atmel_mxt224e/atmel_mxt224e_touchscreen.h"
#include "../../peripherals/touch/host_pen_target_touch_input.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nokia_sea_ray_family_id.h"

namespace cerfcore {

namespace {

class NokiaSeaRayTouchInput : public HostPenTargetTouchInput {
public:
    using HostPenTargetTouchInput::HostPenTargetTouchInput;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay &&
               emu_.Get<BoardParts>().Has(PartId::AtmelMxt224e);
    }

protected:
    HostPenTarget& Target() override { return emu_.Get<AtmelMxt224eTouchscreen>(); }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayTouchInput, TouchInput);

}
