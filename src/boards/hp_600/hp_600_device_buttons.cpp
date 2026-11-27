#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/device_buttons.h>
#include "hp_600_bezel_buttons.h"
#include "hp_600_family_id.h"
#include "hp_600_touch_panel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

class Hp600DeviceButtons : public DeviceButtons {
public:
    using DeviceButtons::DeviceButtons;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
    }

    std::vector<ButtonSpec> BoardButtons() override {
        static constexpr uint16_t                                kBezelAdcX = 989u;
        static constexpr std::array<uint16_t, kHp600BezelZones> kBezelAdcY = {183u, 389u, 595u, 801u};
        const std::array<Hp600BezelName, kHp600BezelZones>& names = emu_.Get<Hp600BezelButtons>().Names();
        std::array<RawTapButton, kHp600BezelZones>          buttons{};
        for (size_t zone = 0; zone < kHp600BezelZones; ++zone) {
            buttons[zone] = RawTapButton{names[zone].id, names[zone].label, kBezelAdcX, kBezelAdcY[zone]};
        }
        return RawTapButtons(buttons, [this](uint16_t x, uint16_t y) { emu_.Get<Hp600TouchPanel>().TapRaw(x, y); });
    }
};

}

REGISTER_SERVICE_AS(Hp600DeviceButtons, DeviceButtons);

}
