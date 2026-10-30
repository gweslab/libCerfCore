#include "samsung_smdk2410_keyboard_controller.h"

#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"
#include "samsung_smdk2410_fn_key.h"
#include "samsung_smdk2410_key_codes.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>

#include <array>

namespace cerfcore {

namespace {

constexpr uint8_t kScancodeKeyUp = 0x80u;

class SamsungSmdk2410KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    void OnReady() override {
        fn_scancode_ = emu_.Get<SamsungSmdk2410FnKey>().Scancode();
        emu_.Get<KeyboardRouter>().Register(this);
    }

    std::string SourceName() const override { return "DevEmu keyboard"; }

    void OnHostKey(uint8_t vk, bool key_up) override {
        auto& map = emu_.Get<KeyboardMap>();
        if (key_up) {
            const uint32_t code = down_[vk];
            if (code == 0) return;
            down_[vk] = 0;
            if (map.HeldLayer(vk) == layer_) layer_ = 0;
            Release(code);
            return;
        }
        if (down_[vk] != 0) {
            Queue(static_cast<uint8_t>(down_[vk] & kSmdk2410ScancodeMask));
            return;
        }
        uint32_t code = 0;
        if ((layer_ == 0 || !map.LayerDeviceCode(vk, layer_, code)) && !map.BaseDeviceCode(vk, code)) return;
        down_[vk] = code;
        if (const uint8_t held = map.HeldLayer(vk)) layer_ = held;
        Press(code);
    }

private:
    void Press(uint32_t code) {
        if (code & kSmdk2410FnChord) PressScancode(fn_scancode_);
        PressScancode(static_cast<uint8_t>(code & kSmdk2410ScancodeMask));
    }

    void Release(uint32_t code) {
        ReleaseScancode(static_cast<uint8_t>(code & kSmdk2410ScancodeMask));
        if (code & kSmdk2410FnChord) ReleaseScancode(fn_scancode_);
    }

    void PressScancode(uint8_t scancode) {
        if (scancode == fn_scancode_ && fn_refs_++ != 0) return;
        Queue(scancode);
    }

    void ReleaseScancode(uint8_t scancode) {
        if (scancode == fn_scancode_ && --fn_refs_ != 0) return;
        Queue(static_cast<uint8_t>(scancode | kScancodeKeyUp));
    }

    void Queue(uint8_t scancode) { emu_.Get<SamsungSmdk2410KeyboardController>().QueueScancode(scancode); }

    std::array<uint32_t, 256> down_{};
    uint8_t                   layer_       = 0;
    uint8_t                   fn_scancode_ = 0;
    int                       fn_refs_     = 0;
};

}

REGISTER_SERVICE(SamsungSmdk2410KeyboardInput);

}
