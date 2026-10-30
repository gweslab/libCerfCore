#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/device_buttons.h>
#include "../../socs/vr41xx/vr41xx_piu.h"
#include <cerfcore/boards/board_context.h>
#include "casio_pocketpostpet_family_id.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cerfcore {

namespace {

struct StripButton { const char* id; const char* label; uint16_t adc_y; };

/* casio_toricomail_ce212 touch.dll sub_137095C (Jx627 table @0x1370568): 7 right-strip hard icons, VK 0xE9..0xEF
   top-to-bottom, each record field[2..3] = its v9 (4x GDI px-Y) zone. */
constexpr uint16_t kStripAdcX = 945u;
constexpr StripButton kStrip[] = {
    { "top_menu",  "Top Menu",   73u },
    { "get_mail",  "Get Mail",  220u },
    { "internet",  "Internet",  367u },
    { "mic",       "Mic",       514u },
    { "tools",     "Tools",     660u },
    { "set_up",    "Set Up",    807u },
    { "text_edit", "Text Edit", 953u },
};

class CasioPocketpostpetDeviceButtons : public DeviceButtons {
public:
    using DeviceButtons::DeviceButtons;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CasioPocketpostpet;
    }

    std::vector<ActionControl> Actions() override {
        std::vector<ActionControl> out;
        for (const StripButton& b : kStrip) {
            ActionControl it;
            it.id    = b.id;
            it.label = b.label;
            it.press = [this, y = b.adc_y] {
                emu_.Get<Vr41xxPiu>().SyntheticTap(kStripAdcX, y);
            };
            out.push_back(std::move(it));
        }
        return out;
    }
};

}

REGISTER_SERVICE_AS(CasioPocketpostpetDeviceButtons, DeviceButtons);

}
