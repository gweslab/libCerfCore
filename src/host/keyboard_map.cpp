#include <cerfcore/host/keyboard_map.h>

#include "pc_special_key_order.h"

namespace cerfcore {

const KeyBinding* KeyboardMap::LayerBinding(uint8_t vk, uint8_t layer) const {
    for (const KeyBinding& b : Bindings())
        if (b.vk == vk && b.layer == layer) return &b;
    return nullptr;
}

std::vector<SpecialKey> KeyboardMap::SpecialKeys() const {
    std::vector<SpecialKey> keys;
    for (const uint8_t vk : PcSpecialKeyOrder())
        if (const KeyBinding* b = LayerBinding(vk, 0)) keys.push_back({vk, b->guest_label});
    return keys;
}

bool KeyboardMap::BaseDeviceCode(uint8_t vk, uint32_t& out) const { return LayerDeviceCode(vk, 0, out); }

bool KeyboardMap::LayerDeviceCode(uint8_t vk, uint8_t layer, uint32_t& out) const {
    const KeyBinding* b = LayerBinding(vk, layer);
    if (b == nullptr) return false;
    out = b->device_code;
    return true;
}

uint8_t KeyboardMap::HeldLayer(uint8_t vk) const {
    const KeyBinding* b = LayerBinding(vk, 0);
    return b == nullptr ? 0 : b->holds_layer;
}

const char* KeyboardMap::GuestLabel(uint8_t vk, uint8_t layer) const {
    const KeyBinding* b = LayerBinding(vk, layer);
    return b == nullptr ? nullptr : b->guest_label;
}

}
