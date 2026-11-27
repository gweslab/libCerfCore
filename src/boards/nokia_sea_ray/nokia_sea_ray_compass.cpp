#include "../../core/board_parts.h"
#include "../../peripherals/akm_ak8975/akm_ak8975.h"
#include "../../peripherals/akm_ak8975/akm_ak8975_part_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nokia_sea_ray_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint8_t kCompassAddress = 0x0Cu;

class NokiaSeaRayCompass : public AkmAk8975 {
public:
    using AkmAk8975::AkmAk8975;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay &&
               emu_.Get<BoardParts>().Has(PartId::AkmAk8975c);
    }

protected:
    uint8_t DeviceAddress() const override { return kCompassAddress; }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayCompass, AkmAk8975);

}
