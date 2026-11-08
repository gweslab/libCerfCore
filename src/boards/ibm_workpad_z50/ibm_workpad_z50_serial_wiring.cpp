#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../../socs/vr41xx/vr41xx_serial_wiring.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "ibm_workpad_z50_family_id.h"

namespace cerfcore {

namespace {

class IbmWorkpadZ50SerialWiring : public Vr41xxSerialWiring {
public:
    using Vr41xxSerialWiring::Vr41xxSerialWiring;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
    }

    std::optional<Vr41xxSerialModem> ForSiu() const override {
        return Vr41xxSerialModem("COM1", Vr41xxGiu::kDcdPin);
    }

    bool SupplyOn() override { return true; }

    void RegisterSupplyListener(std::function<void()>) override {}
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50SerialWiring, Vr41xxSerialWiring);

}
