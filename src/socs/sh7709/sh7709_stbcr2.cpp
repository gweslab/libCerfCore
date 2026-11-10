#include "../sh77xx/sh77xx_power_down_impl.h"
#include "../sh77xx/sh77xx_standby_control.h"
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr cerf_reg_window_detail::RegWindowModel kSh7709Stbcr2 = sh77xx_power_down::Stbcr2(
    0xBFu, 0x00u, cerf_reg_window_detail::OtherReset::kReset);

class Sh7709Stbcr2 : public sh77xx_power_down::ModuleStopRegister<SocId::Sh7709, kSh7709Stbcr2, Sh77xxStbcr2> {
public:
    using ModuleStopRegister::ModuleStopRegister;
};

}

REGISTER_SERVICE_AS(Sh7709Stbcr2, Sh77xxStbcr2);

}
