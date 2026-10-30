#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/device_buttons.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"
#include "hp_jornada_700_touch.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

/* Bezel soft buttons: raw-ADC zone centers per jornada720 touch.dll sub_FB1314 (gate
   X 31..67; Y zones 151-274 / 361-469 / 591-689 / 816-914). */
struct BezelEntry { const char* id; const char* label; uint16_t adc_y; };
constexpr uint16_t kBezelAdcX = 49;
constexpr BezelEntry kBezel[] = {
    { "settings",     "Settings",     212 },
    { "backup",       "HP backup",    415 },
    { "dialer",       "HP dialer",    640 },
    { "media_player", "Media player", 865 },
};

class HpJornada700DeviceButtons : public DeviceButtons {
public:
    using DeviceButtons::DeviceButtons;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    std::vector<ActionControl> Actions() override {
        std::vector<ActionControl> out;
        for (const BezelEntry& b : kBezel) {
            ActionControl it;
            it.id    = b.id;
            it.label = b.label;
            it.press = [this, y = b.adc_y] {
                emu_.Get<HpJornada700Touch>().TapRawAdc(kBezelAdcX, y);
            };
            out.push_back(std::move(it));
        }
        return out;
    }
};

}

REGISTER_SERVICE_AS(HpJornada700DeviceButtons, DeviceButtons);

}
