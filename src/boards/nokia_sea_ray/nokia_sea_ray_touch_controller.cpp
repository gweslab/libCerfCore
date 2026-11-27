#include "../../core/board_parts.h"
#include "../../peripherals/atmel_mxt224e/atmel_mxt224e.h"
#include "../../peripherals/atmel_mxt224e/atmel_mxt224e_part_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nokia_sea_ray_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint8_t kTouchControllerAddress = 0x4Bu;

class NokiaSeaRayTouchController : public AtmelMxt224e {
public:
    using AtmelMxt224e::AtmelMxt224e;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay &&
               emu_.Get<BoardParts>().Has(PartId::AtmelMxt224e);
    }

    bool XLinesFollowSurfaceY() const override { return true; }

protected:
    uint8_t DeviceAddress() const override { return kTouchControllerAddress; }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayTouchController, AtmelMxt224e);

}
