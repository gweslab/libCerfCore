#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/service.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "compaq_aero_1500_family_id.h"

namespace cerfcore {

namespace {

constexpr int kRomTypeStrapGpio19 = 19;
constexpr int kRomTypeStrapGpio20 = 20;

class CompaqAero1500RomTypeStrap : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    void OnReady() override {
        auto& giu = emu_.Get<Vr41xxGiu>();
        giu.SetPinLevel(kRomTypeStrapGpio19, true);
        giu.SetPinLevel(kRomTypeStrapGpio20, true);
    }
};

}

REGISTER_SERVICE(CompaqAero1500RomTypeStrap);

}
