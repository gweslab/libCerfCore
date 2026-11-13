#include "../sh77xx/sh77xx_adc.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "sh7709_id.h"

namespace cerfcore {

namespace {

class Sh7709Adc : public Sh77xxAdc {
public:
    using Sh77xxAdc::Sh77xxAdc;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetSocId() == SocId::Sh7709; }
};

}

REGISTER_SERVICE_AS(Sh7709Adc, Sh77xxAdc);

}
