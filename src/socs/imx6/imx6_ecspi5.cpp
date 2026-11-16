#include "imx6_ecspi.h"
#include <cerfcore/boards/board_context.h>
#include "imx6_variant.h"

namespace cerfcore {

namespace {

class Imx6Ecspi5 final : public Imx6Ecspi<0x02018000u> {
public:
    using Imx6Ecspi::Imx6Ecspi;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd || !SocId::IsImx6(bd->GetSocId())) return false;
        auto* variant = emu_.TryGet<Imx6Variant>();
        return variant && variant->Family() == Imx6Family::DualQuad;
    }

};

}

REGISTER_SERVICE(Imx6Ecspi5);

}
