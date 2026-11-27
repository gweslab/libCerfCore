#include "../../core/board_parts.h"
#include "../../peripherals/atmel_mxt224e/atmel_mxt224e_display_area.h"
#include "../../peripherals/atmel_mxt224e/atmel_mxt224e_part_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nokia_sea_ray_family_id.h"

namespace cerfcore {

namespace {

constexpr AtmelMxt224eDisplayArea::Area kArea{480u, 864u, 480u, 800u};

class NokiaSeaRayTouchDisplayArea : public AtmelMxt224eDisplayArea {
public:
    using AtmelMxt224eDisplayArea::AtmelMxt224eDisplayArea;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay &&
               emu_.Get<BoardParts>().Has(PartId::AtmelMxt224e);
    }

    Area DisplayArea() const override { return kArea; }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayTouchDisplayArea, AtmelMxt224eDisplayArea);

}
