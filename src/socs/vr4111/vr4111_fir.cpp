#include "../vr41xx/vr41xx_fir_impl.h"
#include "vr4111_id.h"

namespace cerfcore {

namespace {

constexpr cerf_reg_window_detail::RegWindowModel kVr4111FirModel =
    cerf_vr41xx_fir_detail::FirModel(0x0C000040u, 0x0000u, false);

class Vr4111Fir : public cerf_vr41xx_fir_detail::Vr41xxFirBase<SocId::Vr4111, kVr4111FirModel> {
public:
    using Vr41xxFirBase::Vr41xxFirBase;
};

}

REGISTER_SERVICE(Vr4111Fir);

}
