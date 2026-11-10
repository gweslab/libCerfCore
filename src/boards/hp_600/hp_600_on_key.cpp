#include <cerfcore/core/service.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "../../socs/sh77xx/sh77xx_intc.h"
#include "hp_600_family_id.h"

namespace cerfcore {

namespace {

class Hp600OnKey : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600; }

    void OnReady() override {
        intc_ = &emu_.Get<Sh77xxIntc>();
        emu_.Get<GuestDeepSleep>().RegisterUserWakeInput([this] {
            intc_->SetPinLevel(Sh77xxIntcPin::kIrq0, false);
            intc_->SetPinLevel(Sh77xxIntcPin::kIrq0, true);
        });
    }

private:
    Sh77xxIntc* intc_ = nullptr;
};

}

REGISTER_SERVICE(Hp600OnKey);

}
