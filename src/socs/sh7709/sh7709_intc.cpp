#include "../sh77xx/sh77xx_intc_impl.h"
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr sh77xx_intc::Model kSh7709Intc{.irls = false};

class Sh7709Intc : public sh77xx_intc::Sh77xxIntcBase<SocId::Sh7709, kSh7709Intc> {
public:
    using Sh77xxIntcBase::Sh77xxIntcBase;
};

}

REGISTER_SERVICE_AS(Sh7709Intc, Sh77xxIntc);

}
