#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/device_buttons.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "hp_600_family_id.h"
#include "hp_600_sh7709_touch_panel.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

class Hp600Sh7709DeviceButtons : public DeviceButtons {
public:
    using DeviceButtons::DeviceButtons;

    bool ShouldRegister() override {
        BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    std::vector<ActionControl> Actions() override {
        static constexpr uint16_t     kBezelAdcX = 989u;
        static constexpr RawTapButton kBezel[]   = {
            {"settings", "Settings", 183u},
            {"mail", "Mail", 389u},
            {"fax", "Fax", 595u},
            {"find", "Find", 801u},
        };
        return RawTapColumn(kBezel, kBezelAdcX,
                            [this](uint16_t x, uint16_t y) { emu_.Get<Hp600Sh7709TouchPanel>().TapRaw(x, y); });
    }
};

}

REGISTER_SERVICE_AS(Hp600Sh7709DeviceButtons, DeviceButtons);

}
