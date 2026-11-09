#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include "compaq_aero_1500_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

const std::vector<KeyBinding> kBindings = {
    { 0x0D, 18, "Action", 0, 0 },
    { 0x1B,  7, "Exit",   0, 0 },
    { 0x26, 16, nullptr,  0, 0 },
    { 0x28, 17, nullptr,  0, 0 },
    { 0x70, 12, "App1",   0, 0 },
    { 0x71, 11, "App2",   0, 0 },
    { 0x72, 10, "App3",   0, 0 },
    { 0x73,  9, "App4",   0, 0 },
    { 0x74,  1, "Record", 0, 0 },
    { 0xDF,  8, "Power",  0, 0 },
};

class CompaqAero1500KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    const std::vector<KeyBinding>& Bindings() const override { return kBindings; }
};

}

REGISTER_SERVICE_AS(CompaqAero1500KeyboardMap, KeyboardMap);

}
