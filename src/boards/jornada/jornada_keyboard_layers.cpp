#include "jornada_keyboard_layers.h"

namespace cerfcore {

namespace jornada_kbd {

void BuildBindings(const uint8_t scancodes[256], std::vector<KeyBinding>& out) {
    for (int vk = 0; vk < 256; ++vk) {
        const uint8_t sc = scancodes[vk];
        if (!sc) continue;
        KeyBinding b{ static_cast<uint8_t>(vk), sc, nullptr, 0, 0 };
        if (vk == kVkFn)  { b.guest_label = "Fn";  b.holds_layer = 1; }
        if (vk == kVkAlt) { b.guest_label = "Alt"; b.holds_layer = 2; }
        out.push_back(b);
    }
    for (const LayerKey& k : kFnLayer)  out.push_back({ k.vk, 0, k.label, 1, 0 });
    for (const LayerKey& k : kAltLayer) out.push_back({ k.vk, 0, k.label, 2, 0 });
    for (const LayerKey& k : kNumLayer)
        out.push_back({ k.vk, 0, k.label, kNumLockLayer, 0 });
}

std::vector<KeyboardToggleLayer> ToggleLayers() {
    return { { kNumLockLayer, "Num Lock" } };
}

std::vector<uint8_t> SpecialKeyOrder() {
    return { 0x0D, 0x5B, 0x11, kVkAlt, 0x10, kVkFn, 0x1B, 0x09, 0x25, 0x26, 0x28, 0x27, 0x2E };
}

}

}
