#include "../sh77xx/sh77xx_scif.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr Sh77xxScifChannel kScif2{kSh77xxScif2Base,
                                   "SCIF2",
                                   Sh77xxIntcSource::kEri2,
                                   Sh77xxIntcSource::kRxi2,
                                   Sh77xxIntcSource::kBri2,
                                   Sh77xxIntcSource::kTxi2,
                                   0x02u,
                                   4u,
                                   false};

class Sh7709Scif : public Sh77xxScif {
public:
    using Sh77xxScif::Sh77xxScif;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetSocId() == SocId::Sh7709; }

protected:
    const Sh77xxScifChannel& Channel() const override { return kScif2; }
};

}

REGISTER_SERVICE(Sh7709Scif);

}
