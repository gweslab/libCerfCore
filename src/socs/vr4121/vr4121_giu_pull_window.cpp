#include "../vr41xx/vr41xx_giu_pull_window_impl.h"

#include "vr4121_id.h"

namespace cerfcore {

namespace {

class Vr4121GiuPullWindow : public cerf_vr41xx_giu_detail::Vr41xxGiuPullWindowBase<SocId::Vr4121> {
public:
    using Vr41xxGiuPullWindowBase::Vr41xxGiuPullWindowBase;
};

}

REGISTER_SERVICE(Vr4121GiuPullWindow);

}
