#include "../sh77xx/sh77xx_ccr_impl.h"
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr cerf_reg_window_detail::RegWindowModel kSh7709Ccr =
    sh77xx_ccr::Window(0x0027u, 0xFFF0u, cerf_reg_window_detail::OtherReset::kReset);

class Sh7709Ccr : public cerf_reg_window_detail::RegWindowBase<SocId::Sh7709, kSh7709Ccr> {
public:
    using RegWindowBase::RegWindowBase;
};

}

REGISTER_SERVICE(Sh7709Ccr);

}
