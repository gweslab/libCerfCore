#include "../sh77xx/sh77xx_wdt_impl.h"
#include "sh7729_id.h"

namespace cerfcore {

namespace {

class Sh7729Wdt : public sh77xx_wdt::Sh77xxWdtBase<SocId::Sh7729> {
public:
    using Sh77xxWdtBase::Sh77xxWdtBase;
};

}

REGISTER_SERVICE_AS(Sh7729Wdt, Sh77xxWdt);

}
