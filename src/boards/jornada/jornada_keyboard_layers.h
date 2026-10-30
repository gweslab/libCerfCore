#pragma once

#include <cerfcore/host/keyboard_map.h>

#include <cstdint>
#include <span>
#include <vector>

namespace cerfcore {

namespace jornada_kbd {

inline constexpr uint8_t kVkFn  = 0x7B;
inline constexpr uint8_t kVkAlt = 0x12;

struct LayerKey { uint8_t vk; const char* label; };

/* Layer 1 - Fn held. {}[] are word_FC10A0 col7 on the 720. */
inline constexpr LayerKey kFnLayer[] = {
    { 0x50, "{" }, { 0xDC, "}" }, { 0xBA, "[" }, { 0xDE, "]" },
    { 0x10, "Caps Lock" },
    { 0x11, "Num Lock"  },
};

/* Layer 2 - Alt held. */
inline constexpr LayerKey kAltLayer[] = {
    { 0x26, "PgUp" }, { 0x25, "Home" }, { 0x28, "PgDn" }, { 0x27, "End" },
};

void BuildBindings(const uint8_t scancodes[256], std::span<const LayerKey> labels, std::vector<KeyBinding>& out);

}  /* namespace jornada_kbd */

}
