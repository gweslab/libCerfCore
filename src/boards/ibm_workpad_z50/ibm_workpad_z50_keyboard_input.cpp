#include "../nec_vr41xx_keyboard_input.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "ibm_workpad_z50_family_id.h"

namespace cerfcore {

namespace {

class IbmWorkpadZ50KeyboardInput : public NecVr41xxKeyboardInput {
public:
    using NecVr41xxKeyboardInput::NecVr41xxKeyboardInput;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
    }
};

}

REGISTER_SERVICE(IbmWorkpadZ50KeyboardInput);

}
