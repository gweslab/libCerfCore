#include "../../core/board_parts.h"
#include "../../peripherals/atmel_mxt224e/atmel_mxt224e_part_id.h"
#include "../../peripherals/atmel_mxt224e/atmel_mxt224e_touchscreen.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/device_buttons.h>
#include "nokia_sea_ray_family_id.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint16_t Centre(uint16_t low, uint16_t high) { return static_cast<uint16_t>((low + high) / 2u); }

constexpr uint16_t kKeyTop    = 810u;
constexpr uint16_t kKeyBottom = 864u;

class NokiaSeaRayDeviceButtons : public DeviceButtons {
public:
    using DeviceButtons::DeviceButtons;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay &&
               emu_.Get<BoardParts>().Has(PartId::AtmelMxt224e);
    }

    std::vector<ButtonSpec> BoardButtons() override {
        static constexpr RawTapButton kKeys[] = {
            {"back", "Back", Centre(0u, 137u), Centre(kKeyTop, kKeyBottom)},
            {"start", "Start", Centre(203u, 275u), Centre(kKeyTop, kKeyBottom)},
            {"search", "Search", Centre(341u, 480u), Centre(kKeyTop, kKeyBottom)},
        };
        return RawTapButtons(kKeys, [this](uint16_t x, uint16_t y) {
            emu_.Get<AtmelMxt224eTouchscreen>().TapTouchSpace(x, y);
        });
    }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayDeviceButtons, DeviceButtons);

}
