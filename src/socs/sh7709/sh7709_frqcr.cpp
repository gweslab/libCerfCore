#include "../sh77xx/sh77xx_frqcr_impl.h"
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr cerf_reg_window_detail::RegWindowModel kSh7709Frqcr = sh77xx_frqcr::Window(0xE3FFu, 0x1C00u);

class Sh7709Frqcr : public sh77xx_frqcr::Sh77xxFrqcrBase<SocId::Sh7709, kSh7709Frqcr> {
public:
    using Sh77xxFrqcrBase::Sh77xxFrqcrBase;
};

}

REGISTER_SERVICE_AS(Sh7709Frqcr, Sh77xxFrqcr);

}
