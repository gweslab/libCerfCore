#pragma once

#include <cerfcore/host/keyboard_map.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

/* Shared HP Jornada (720 + 820) keyboard preview layers: Fn (host F10), Alt
   (host Alt), and a Num Lock numpad overlay. Labels are display-only. */
namespace jornada_kbd {

inline constexpr uint8_t kVkFn         = 0x79;
inline constexpr uint8_t kVkAlt        = 0x12;
inline constexpr uint8_t kNumLockLayer = 3;

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

/* Layer 3 - Num Lock numpad overlay. */
inline constexpr LayerKey kNumLayer[] = {
    { 0x37, "7" }, { 0x38, "8" }, { 0x39, "9" }, { 0x30, "/" },
    { 0x55, "4" }, { 0x49, "5" }, { 0x4F, "6" }, { 0x50, "*" },
    { 0x4A, "1" }, { 0x4B, "2" }, { 0x4C, "3" }, { 0xBA, "-" },
    { 0x4D, "0" }, { 0xBC, "," }, { 0xBE, "." }, { 0xBF, "+" },
};

void                             BuildBindings(const uint8_t scancodes[256], std::vector<KeyBinding>& out);
std::vector<KeyboardToggleLayer> ToggleLayers();
std::vector<uint8_t>             SpecialKeyOrder();

}  /* namespace jornada_kbd */

}
