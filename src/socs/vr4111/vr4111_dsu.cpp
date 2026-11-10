#include "../vr41xx/vr41xx_dsu_impl.h"
#include "vr4111_id.h"

namespace cerfcore {

namespace {

class Vr4111Dsu : public cerf_vr41xx_dsu_detail::Vr41xxDsuBase<SocId::Vr4111> {
public:
    using Vr41xxDsuBase::Vr41xxDsuBase;
};

}

REGISTER_SERVICE(Vr4111Dsu);

}
