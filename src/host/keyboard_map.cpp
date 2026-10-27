#include <cerfcore/host/keyboard_map.h>

namespace cerfcore {

const KeyBinding* KeyboardMap::BaseBinding(uint8_t vk) const {
    for (const KeyBinding& b : Bindings())
        if (b.vk == vk && b.layer == 0) return &b;
    return nullptr;
}

std::vector<SpecialKey> KeyboardMap::SpecialKeys() const {
    std::vector<SpecialKey> keys;
    for (const uint8_t vk : SpecialKeyOrder())
        if (const KeyBinding* b = BaseBinding(vk)) keys.push_back({vk, b->guest_label});
    return keys;
}

bool KeyboardMap::BaseDeviceCode(uint8_t vk, uint32_t& out) const {
    const KeyBinding* b = BaseBinding(vk);
    if (b == nullptr) return false;
    out = b->device_code;
    return true;
}

}
