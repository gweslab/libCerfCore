#include "../../core/board_parts.h"
#include "../../peripherals/sony_imx125/sony_imx125.h"
#include "../../peripherals/sony_imx125/sony_imx125_part_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nokia_sea_ray_camera_sensor_address.h"
#include "nokia_sea_ray_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

class NokiaSeaRayImx125Camera : public SonyImx125 {
public:
    using SonyImx125::SonyImx125;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay &&
               emu_.Get<BoardParts>().Has(PartId::SonyImx125);
    }

protected:
    uint8_t DeviceAddress() const override { return kNokiaSeaRayCameraSensorAddress; }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayImx125Camera, SmiaCameraSensor);

}
