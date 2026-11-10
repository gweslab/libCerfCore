#include "../sh77xx/sh77xx_wdt_impl.h"
#include "sh7709_id.h"

namespace cerfcore {

namespace {

class Sh7709Wdt : public sh77xx_wdt::Sh77xxWdtBase<SocId::Sh7709> {
public:
    using Sh77xxWdtBase::Sh77xxWdtBase;
};

}

REGISTER_SERVICE_AS(Sh7709Wdt, Sh77xxWdt);

}
