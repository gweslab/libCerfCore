#include "vr4111_core_clock_model.h"
#include "vr4111_id.h"

namespace cerfcore {

namespace {

class Vr4111CoreClock : public Vr41xxCoreClockBase<SocId::Vr4111, kVr4111CoreClock> {
public:
    using Vr41xxCoreClockBase::Vr41xxCoreClockBase;
};

}

REGISTER_SERVICE_AS(Vr4111CoreClock, MipsCoreClock);

}
