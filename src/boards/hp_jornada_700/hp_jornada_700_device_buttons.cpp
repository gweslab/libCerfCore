#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/device_buttons.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"
#include "hp_jornada_700_touch.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

class HpJornada700DeviceButtons : public DeviceButtons {
public:
    using DeviceButtons::DeviceButtons;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    std::vector<ActionControl> Actions() override {
        static constexpr uint16_t     kBezelAdcX = 49;
        static constexpr RawTapButton kBezel[]   = {
            { "settings",     "Settings",     212 },
            { "backup",       "HP backup",    415 },
            { "dialer",       "HP dialer",    640 },
            { "media_player", "Media player", 865 },
        };
        return RawTapColumn(kBezel, kBezelAdcX,
                            [this](uint16_t x, uint16_t y) { emu_.Get<HpJornada700Touch>().TapRawAdc(x, y); });
    }
};

}

REGISTER_SERVICE_AS(HpJornada700DeviceButtons, DeviceButtons);

}
