#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/device_buttons.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"
#include "hp_jornada_700_keyboard.h"
#include "hp_jornada_700_touch.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

namespace {

struct KeyEntry { const char* label; uint8_t vk; };

constexpr KeyEntry kAppRow[] = {
    { "Internet Explorer", 0xC1 }, { "E-mail",         0xC2 },
    { "Voice Recorder",    0xC3 }, { "Contacts",       0xC4 },
    { "Tasks",             0xC5 }, { "Calendar",       0xC6 },
    { "HP quick view",     0xC7 }, { "HP quick pad",   0xC8 },
    { "Pocket Access",     0xC9 }, { "Pocket Excel",   0xCA },
    { "Pocket Word",       0xCB },
};
constexpr KeyEntry kMediaKeys[] = {
    { "Volume up", 0xD1 }, { "Volume down", 0xD2 }, { "Play / pause", 0xD3 },
};

/* Bezel soft buttons: raw-ADC zone centers per jornada720 touch.dll sub_FB1314 (gate
   X 31..67; Y zones 151-274 / 361-469 / 591-689 / 816-914). */
struct BezelEntry { const char* label; uint16_t adc_y; };
constexpr uint16_t kBezelAdcX = 49;
constexpr BezelEntry kBezel[] = {
    { "Settings",     212 },
    { "HP backup",    415 },
    { "HP dialer",    640 },
    { "Media player", 865 },
};

class HpJornada700DeviceButtons : public DeviceButtons {
public:
    using DeviceButtons::DeviceButtons;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    std::vector<ButtonGroup> ButtonGroups() override {
        return { KeyRow(std::begin(kAppRow),    std::end(kAppRow)),
                 KeyRow(std::begin(kMediaKeys), std::end(kMediaKeys)),
                 BezelRow() };
    }

private:
    void InjectKey(uint8_t vk) {
        auto& kbd = emu_.Get<HpJornada700Keyboard>();
        kbd.OnHostKey(vk, false);
        kbd.OnHostKey(vk, true);
    }

    ButtonGroup KeyRow(const KeyEntry* first, const KeyEntry* last) {
        ButtonGroup group;
        for (const KeyEntry* k = first; k != last; ++k) {
            ActionControl it;
            it.id    = "key:" + std::to_string(k->vk);
            it.label = k->label;
            it.press = [this, vk = k->vk] { InjectKey(vk); };
            group.push_back(std::move(it));
        }
        return group;
    }

    ButtonGroup BezelRow() {
        ButtonGroup group;
        for (const BezelEntry& b : kBezel) {
            ActionControl it;
            it.id    = "bezel:" + std::to_string(b.adc_y);
            it.label = b.label;
            it.press = [this, y = b.adc_y] {
                emu_.Get<HpJornada700Touch>().TapRawAdc(kBezelAdcX, y);
            };
            group.push_back(std::move(it));
        }
        return group;
    }
};

}

REGISTER_SERVICE_AS(HpJornada700DeviceButtons, DeviceButtons);

}
