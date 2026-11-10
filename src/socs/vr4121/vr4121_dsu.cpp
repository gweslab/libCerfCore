#include "../vr41xx/vr41xx_dsu_impl.h"
#include "vr4121_id.h"

namespace cerfcore {

namespace {

class Vr4121Dsu : public cerf_vr41xx_dsu_detail::Vr41xxDsuBase<SocId::Vr4121> {
public:
    using Vr41xxDsuBase::Vr41xxDsuBase;
};

}

REGISTER_SERVICE(Vr4121Dsu);

}
