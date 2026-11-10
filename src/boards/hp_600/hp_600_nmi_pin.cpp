#include "../../socs/sh77xx/sh77xx_nmi_pin.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "hp_600_family_id.h"

namespace cerfcore {

namespace {

class Hp600NmiPin : public Sh77xxNmiPin {
public:
    using Sh77xxNmiPin::Sh77xxNmiPin;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
    }

    bool High() const override { return true; }
};

}

REGISTER_SERVICE_AS(Hp600NmiPin, Sh77xxNmiPin);

}
