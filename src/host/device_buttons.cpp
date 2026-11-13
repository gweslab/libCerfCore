#include <cerfcore/host/device_buttons.h>

#include <utility>

namespace cerfcore {

std::vector<ActionControl> DeviceButtons::RawTapColumn(std::span<const RawTapButton> buttons, uint16_t adc_x,
                                                       std::function<void(uint16_t, uint16_t)> tap) const {
    std::vector<ActionControl> out;
    for (const RawTapButton& b : buttons) {
        ActionControl it;
        it.id    = b.id;
        it.label = b.label;
        it.press = [tap, adc_x, y = b.adc_y] { tap(adc_x, y); };
        out.push_back(std::move(it));
    }
    return out;
}

}
