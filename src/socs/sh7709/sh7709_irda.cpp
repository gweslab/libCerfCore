#include "../sh77xx/sh77xx_scif.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr Sh77xxScifChannel kIrda{kSh77xxIrdaBase,
                                  "IrDA",
                                  Sh77xxIntcSource::kEri1,
                                  Sh77xxIntcSource::kRxi1,
                                  Sh77xxIntcSource::kBri1,
                                  Sh77xxIntcSource::kTxi1,
                                  0x01u,
                                  2u,
                                  true};

class Sh7709Irda : public Sh77xxScif {
public:
    using Sh77xxScif::Sh77xxScif;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetSocId() == SocId::Sh7709; }

protected:
    const Sh77xxScifChannel& Channel() const override { return kIrda; }
};

}

REGISTER_SERVICE(Sh7709Irda);

}
