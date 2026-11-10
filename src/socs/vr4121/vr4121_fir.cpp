#include "../vr41xx/vr41xx_fir_impl.h"
#include "vr4121_id.h"

namespace cerfcore {

namespace {

constexpr cerf_reg_window_detail::RegWindowModel kVr4121FirModel =
    cerf_vr41xx_fir_detail::FirModel(0x0C000040u, 0x0000u, false);

class Vr4121Fir : public cerf_vr41xx_fir_detail::Vr41xxFirBase<SocId::Vr4121, kVr4121FirModel> {
public:
    using Vr41xxFirBase::Vr41xxFirBase;
};

}

REGISTER_SERVICE(Vr4121Fir);

}
