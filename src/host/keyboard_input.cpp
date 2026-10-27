#include <cerfcore/host/keyboard_input.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>

namespace cerfcore {

bool KeyboardInput::CanDeliverVk(uint8_t vk) const {
    auto* map = emu_.TryGet<KeyboardMap>();
    uint32_t code;
    return map && map->BaseDeviceCode(vk, code);
}

std::vector<SpecialKey> KeyboardInput::SpecialKeys() const { return emu_.Get<KeyboardMap>().SpecialKeys(); }

bool KeyboardInput::HasLetterKeys() const {
    for (uint8_t vk = 'A'; vk <= 'Z'; ++vk)
        if (!CanDeliverVk(vk)) return false;
    return true;
}

}
