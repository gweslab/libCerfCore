#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include "../nec_vr41xx_keyboard_input.h"
#include "ibm_workpad_z50_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

constexpr uint8_t kAltLayer = 1;

const std::vector<KeyBinding> kBindings = {
    { 0x70,  0, "A1",  0, 0 }, { 0x71,  8, "A2",  0, 0 }, { 0x72,  1, "A3",  0, 0 },
    { 0x73,  9, "A4",  0, 0 }, { 0x74,  2, "A5",  0, 0 }, { 0x75, 10, "A6",  0, 0 },
    { 0x76,  3, "A7",  0, 0 }, { 0x77, 11, "A8",  0, 0 }, { 0x78,  4, "A9",  0, 0 },
    { 0x79, 12, "A10", 0, 0 }, { 0x7A,  7, "A11", 0, 0 }, { 0x7B, 15, "A12", 0, 0 },
    { 0xDE, 16, nullptr, 0, 0 }, { 0xDB, 17, nullptr, 0, 0 }, { 0xBD, 18, nullptr, 0, 0 },
    { 0x30, 19, nullptr, 0, 0 }, { 0x50, 20, nullptr, 0, 0 }, { 0xBA, 21, nullptr, 0, 0 },
    { 0x26, 22, nullptr, 0, 0 }, { 0xBF, 23, nullptr, 0, 0 }, { 0x39, 27, nullptr, 0, 0 },
    { 0x4F, 28, nullptr, 0, 0 }, { 0x4C, 29, nullptr, 0, 0 }, { 0xBE, 30, nullptr, 0, 0 },
    { 0x25, 32, nullptr, 0, 0 }, { 0xDD, 33, nullptr, 0, 0 }, { 0xBB, 34, nullptr, 0, 0 },
    { 0x38, 35, nullptr, 0, 0 }, { 0x49, 36, nullptr, 0, 0 }, { 0x4B, 37, nullptr, 0, 0 },
    { 0xBC, 38, nullptr, 0, 0 }, { 0x48, 40, nullptr, 0, 0 }, { 0x59, 41, nullptr, 0, 0 },
    { 0x36, 42, nullptr, 0, 0 }, { 0x37, 43, nullptr, 0, 0 }, { 0x55, 44, nullptr, 0, 0 },
    { 0x4A, 45, nullptr, 0, 0 }, { 0x4D, 46, nullptr, 0, 0 }, { 0x4E, 47, nullptr, 0, 0 },
    { 0x08, 49, nullptr, 0, 0 }, { 0x90, 50, nullptr, 0, 0 }, { 0x2E, 51, nullptr, 0, 0 },
    { 0xDC, 53, nullptr, 0, 0 }, { 0x0D, 54, nullptr, 0, 0 }, { 0x20, 55, nullptr, 0, 0 },
    { 0x47, 56, nullptr, 0, 0 }, { 0x54, 57, nullptr, 0, 0 }, { 0x35, 58, nullptr, 0, 0 },
    { 0x34, 59, nullptr, 0, 0 }, { 0x52, 60, nullptr, 0, 0 }, { 0x46, 61, nullptr, 0, 0 },
    { 0x56, 62, nullptr, 0, 0 }, { 0x42, 63, nullptr, 0, 0 }, { 0x33, 67, nullptr, 0, 0 },
    { 0x45, 68, nullptr, 0, 0 }, { 0x44, 69, nullptr, 0, 0 }, { 0x43, 70, nullptr, 0, 0 },
    { 0x27, 71, nullptr, 0, 0 }, { 0x32, 75, nullptr, 0, 0 },
    { 0x57, 76, nullptr, 0, 0 }, { 0x53, 77, nullptr, 0, 0 }, { 0x58, 78, nullptr, 0, 0 },
    { 0x28, 79, nullptr, 0, 0 }, { 0x1B, 80, nullptr, 0, 0 }, { 0x09, 81, nullptr, 0, 0 },
    { 0xC0, 82, nullptr, 0, 0 }, { 0x31, 83, nullptr, 0, 0 }, { 0x51, 84, nullptr, 0, 0 },
    { 0x41, 85, nullptr, 0, 0 }, { 0x5A, 86, nullptr, 0, 0 }, { 0x5B, 88, nullptr, 0, 0 },
    { 0x10, 89, nullptr, 0, 0 }, { 0xA0, 89, nullptr, 0, 0 }, { 0x11, 90, nullptr, 0, 0 },
    { 0xA2, 90, nullptr, 0, 0 }, { 0xA3, 91, nullptr, 0, 0 }, { 0xA1, 94, nullptr, 0, 0 },
    { 0x12, 92, "Alt", 0, kAltLayer },
    { 0xA4, 92, "Alt", 0, kAltLayer },
    { 0xA5, 93, "Alt", 0, kAltLayer },
    { 0xDF, kNecVr41xxPowerSwitchCode, "Power", 0, 0 },
    { 0x26, 0, "PgUp", kAltLayer, 0 },
    { 0x25, 0, "Home", kAltLayer, 0 },
    { 0x28, 0, "PgDn", kAltLayer, 0 },
    { 0x27, 0, "End",  kAltLayer, 0 },
};

class IbmWorkpadZ50KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
    }

    const std::vector<KeyBinding>& Bindings() const override { return kBindings; }
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50KeyboardMap, KeyboardMap);

}
