#include "../../core/board_parts.h"
#include "../../peripherals/toshiba_jt8ew9/toshiba_jt8ew9.h"
#include "../../peripherals/toshiba_jt8ew9/toshiba_jt8ew9_part_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nokia_sea_ray_camera_sensor_address.h"
#include "nokia_sea_ray_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

class NokiaSeaRayJt8ew9Camera : public ToshibaJt8ew9 {
public:
    using ToshibaJt8ew9::ToshibaJt8ew9;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay &&
               emu_.Get<BoardParts>().Has(PartId::ToshibaJt8ew9);
    }

protected:
    uint8_t DeviceAddress() const override { return kNokiaSeaRayCameraSensorAddress; }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayJt8ew9Camera, SmiaCameraSensor);

}
