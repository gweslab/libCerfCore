#include "jornada_keyboard_layers.h"

namespace cerfcore {

namespace jornada_kbd {

void BuildBindings(const uint8_t scancodes[256], std::span<const LayerKey> labels, std::vector<KeyBinding>& out) {
    for (int vk = 0; vk < 256; ++vk) {
        const uint8_t sc = scancodes[vk];
        if (!sc) continue;
        KeyBinding b{ static_cast<uint8_t>(vk), sc, nullptr, 0, 0 };
        if (vk == kVkFn)  { b.guest_label = "Fn";  b.holds_layer = 1; }
        if (vk == kVkAlt) { b.guest_label = "Alt"; b.holds_layer = 2; }
        for (const LayerKey& k : labels)
            if (k.vk == vk) b.guest_label = k.label;
        out.push_back(b);
    }
    for (const LayerKey& k : kFnLayer)  out.push_back({ k.vk, 0, k.label, 1, 0 });
    for (const LayerKey& k : kAltLayer) out.push_back({ k.vk, 0, k.label, 2, 0 });
}

}

}
