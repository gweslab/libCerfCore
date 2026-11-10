#include "../vr41xx/vr41xx_fir_impl.h"
#include "vr4102_id.h"

namespace cerfcore {

namespace {

constexpr cerf_reg_window_detail::RegWindowModel kVr4102FirModel =
    cerf_vr41xx_fir_detail::FirModel(0x0C000040u, 0x0004u, true);

class Vr4102Fir : public cerf_vr41xx_fir_detail::Vr41xxFirBase<SocId::Vr4102, kVr4102FirModel> {
public:
    using Vr41xxFirBase::Vr41xxFirBase;
};

}

REGISTER_SERVICE(Vr4102Fir);

}
