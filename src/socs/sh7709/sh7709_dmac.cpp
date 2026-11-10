#include "../sh77xx/sh77xx_dmac.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "sh7709_id.h"

namespace cerfcore {

namespace {

class Sh7709Dmac : public Sh77xxDmac {
public:
    using Sh77xxDmac::Sh77xxDmac;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetSocId() == SocId::Sh7709; }
};

}

REGISTER_SERVICE_AS(Sh7709Dmac, Sh77xxDmac);

}
