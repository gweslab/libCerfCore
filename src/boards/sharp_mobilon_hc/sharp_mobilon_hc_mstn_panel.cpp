#include "../../socs/pr31x00/pr31x00_panel.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../core/board_config.h"
#include "../lcd_panel_param_id.h"
#include "sharp_mobilon_hc_family_id.h"

namespace cerfcore {

namespace {

class SharpMobilonHcMstnPanel : public Pr31x00Panel {
public:
    using Pr31x00Panel::Pr31x00Panel;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc &&
               emu_.Get<BoardConfig>().EnumId(BoardConfigParamId::LcdPanel) == "monochrome_stn";
    }

    bool Color() const override { return false; }
};

}

REGISTER_SERVICE_AS(SharpMobilonHcMstnPanel, Pr31x00Panel);

}
