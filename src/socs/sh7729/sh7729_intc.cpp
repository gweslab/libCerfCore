#include "../sh77xx/sh77xx_intc_impl.h"
#include "sh7729_id.h"

namespace cerfcore {

namespace {

constexpr sh77xx_intc::Model kSh7729Intc{.irls = true};

class Sh7729Intc : public sh77xx_intc::Sh77xxIntcBase<SocId::Sh7729, kSh7729Intc> {
public:
    using Sh77xxIntcBase::Sh77xxIntcBase;
};

}

REGISTER_SERVICE_AS(Sh7729Intc, Sh77xxIntc);

}
