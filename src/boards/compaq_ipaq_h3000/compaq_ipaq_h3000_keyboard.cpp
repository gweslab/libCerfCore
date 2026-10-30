#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include <cerfcore/boards/board_context.h>
#include "compaq_ipaq_h3000_button_codes.h"
#include "compaq_ipaq_h3000_family_id.h"
#include "compaq_ipaq_h3000_gpio_buttons.h"
#include "compaq_ipaq_h3000_micro_keys.h"

#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

class CompaqIpaqH3000Keyboard : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    std::string SourceName() const override { return "iPAQ buttons"; }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        if (code & kIpaqH3000GpioButtonFlag) {
            emu_.Get<CompaqIpaqH3000GpioButtons>().SetPressed(code & kIpaqH3000ButtonCodeMask,
                                                               !key_up);
            return;
        }
        emu_.Get<CompaqIpaqH3000MicroKeys>().SetPressed(static_cast<uint8_t>(code), !key_up);
    }
};

}

REGISTER_SERVICE(CompaqIpaqH3000Keyboard);

}
