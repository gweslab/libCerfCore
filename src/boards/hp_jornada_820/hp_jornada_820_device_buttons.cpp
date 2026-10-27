#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/device_buttons.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_820_family_id.h"
#include "hp_jornada_820_keyboard.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

namespace {

struct KeyEntry { const char* label; uint8_t vk; };

constexpr KeyEntry kAppRow[] = {
    { "Inbox",        0xC1 }, { "Internet Explorer", 0xC2 },
    { "Calendar",     0xC3 }, { "Contacts",          0xC4 },
    { "Tasks",        0xC5 }, { "Pocket Word",       0xC6 },
    { "Pocket Excel", 0xC7 }, { "Pocket PowerPoint", 0xC8 },
    { "HP viewer",    0xC9 }, { "Voice Recorder",    0xCA },
    { "HP settings",  0xCB },
};

class HpJornada820DeviceButtons : public DeviceButtons {
public:
    using DeviceButtons::DeviceButtons;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada820;
    }

    std::vector<ButtonGroup> ButtonGroups() override {
        ButtonGroup group;
        for (const KeyEntry& k : kAppRow) {
            ActionControl it;
            it.id    = "key:" + std::to_string(k.vk);
            it.label = k.label;
            it.press = [this, vk = k.vk] {
                auto& kbd = emu_.Get<HpJornada820Keyboard>();
                kbd.OnHostKey(vk, false);
                kbd.OnHostKey(vk, true);
            };
            group.push_back(std::move(it));
        }
        return { std::move(group) };
    }
};

}

REGISTER_SERVICE_AS(HpJornada820DeviceButtons, DeviceButtons);

}
