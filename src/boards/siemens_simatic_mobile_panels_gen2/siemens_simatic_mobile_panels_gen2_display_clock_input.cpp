#include "../../socs/imx6/imx6_display_clock_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mobile_panels_gen2_board_profile.h"
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

class SiemensSimaticMobilePanelsGen2DisplayClockInput : public Imx6DisplayClockInput {
public:
    using Imx6DisplayClockInput::Imx6DisplayClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }

    uint64_t PixelClockHz() const override {
        return SiemensSimaticMobilePanelsGen2BoardProfileFor(emu_.Get<BoardContext>().GetBoardId()).timing.pixel_clock_hz;
    }
};

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2DisplayClockInput, Imx6DisplayClockInput);

}
