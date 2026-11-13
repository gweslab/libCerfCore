#include "../sh77xx/sh77xx_dac_impl.h"
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr cerf_reg_window_detail::RegWindowModel kSh7709Dac =
    sh77xx_dac::WindowOf(cerf_reg_window_detail::OtherReset::kReset);

class Sh7709Dac : public sh77xx_dac::Sh77xxDacBase<SocId::Sh7709, kSh7709Dac> {
public:
    using Sh77xxDacBase::Sh77xxDacBase;
};

}

REGISTER_SERVICE_AS(Sh7709Dac, Sh77xxDac);

}
