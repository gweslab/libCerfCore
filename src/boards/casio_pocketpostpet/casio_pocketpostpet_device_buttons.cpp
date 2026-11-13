#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/device_buttons.h>
#include "../../socs/vr41xx/vr41xx_piu.h"
#include <cerfcore/boards/board_context.h>
#include "casio_pocketpostpet_family_id.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

class CasioPocketpostpetDeviceButtons : public DeviceButtons {
public:
    using DeviceButtons::DeviceButtons;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CasioPocketpostpet;
    }

    std::vector<ActionControl> Actions() override {
        static constexpr uint16_t     kStripAdcX = 945u;
        static constexpr RawTapButton kStrip[]   = {
            { "top_menu",  "Top Menu",   73u },
            { "get_mail",  "Get Mail",  220u },
            { "internet",  "Internet",  367u },
            { "mic",       "Mic",       514u },
            { "tools",     "Tools",     660u },
            { "set_up",    "Set Up",    807u },
            { "text_edit", "Text Edit", 953u },
        };
        return RawTapColumn(kStrip, kStripAdcX,
                            [this](uint16_t x, uint16_t y) { emu_.Get<Vr41xxPiu>().SyntheticTap(x, y); });
    }
};

}

REGISTER_SERVICE_AS(CasioPocketpostpetDeviceButtons, DeviceButtons);

}
