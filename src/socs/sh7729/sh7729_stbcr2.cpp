#include "../sh77xx/sh77xx_power_down_impl.h"
#include "sh7729_id.h"

namespace cerfcore {

namespace {

constexpr cerf_reg_window_detail::RegWindowModel kSh7729Stbcr2 = sh77xx_power_down::Stbcr2(
    0x7Fu, 0x80u, cerf_reg_window_detail::OtherReset::kRetain);

class Sh7729Stbcr2 : public cerf_reg_window_detail::RegWindowBase<SocId::Sh7729, kSh7729Stbcr2> {
public:
    using RegWindowBase::RegWindowBase;
};

}

REGISTER_SERVICE(Sh7729Stbcr2);

}
