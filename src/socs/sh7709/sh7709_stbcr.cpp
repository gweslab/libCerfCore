#include "../sh77xx/sh77xx_power_down_impl.h"
#include "../sh77xx/sh77xx_standby_control.h"
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr cerf_reg_window_detail::RegWindowModel kSh7709Stbcr = sh77xx_power_down::Stbcr(
    cerf_reg_window_detail::OtherReset::kReset);

class Sh7709Stbcr : public sh77xx_power_down::StandbyControlRegister<SocId::Sh7709, kSh7709Stbcr> {
public:
    using StandbyControlRegister::StandbyControlRegister;
};

}

REGISTER_SERVICE_AS(Sh7709Stbcr, Sh77xxStbcr);

}
