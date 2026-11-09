#include "compaq_ipaq_h3000_panel.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../core/board_config.h"
#include "../lcd_panel_param_id.h"
#include "compaq_ipaq_h3000_family_id.h"

namespace cerfcore {

namespace {

class CompaqIpaqH31xxStnPanel : public CompaqIpaqH3000Panel {
public:
    using CompaqIpaqH3000Panel::CompaqIpaqH3000Panel;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000 &&
               emu_.Get<BoardConfig>().EnumId(BoardConfigParamId::LcdPanel) == "monochrome_stn";
    }

    /* Linux arch/arm/mach-sa1100/h3100.c h3100_lcd_info:
       .lccr0 = LCCR0_Mono | LCCR0_4PixMono | LCCR0_Sngl | LCCR0_Pas. */
    Kind PanelKind() const override { return Kind{ false, true }; }
};

}

REGISTER_SERVICE_AS(CompaqIpaqH31xxStnPanel, CompaqIpaqH3000Panel);

}
