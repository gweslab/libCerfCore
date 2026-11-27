#include <cerfcore/host/device_buttons.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_pointer_input_lock.h>
#include <cerfcore/host/machine_object_tree.h>

#include <utility>

namespace cerfcore {

bool DeviceButtons::WiredButton::Pressed() { return emu_.Get<HostPointerInputLock>().AdmitUnlessHeld(spec_.press); }

void DeviceButtons::OnReady() {
    auto& tree = emu_.Get<MachineObjectTree>();
    for (ButtonSpec& spec : BoardButtons()) {
        buttons_.push_back(std::make_unique<WiredButton>(emu_, std::move(spec)));
        tree.Add(buttons_.back().get());
    }
}

std::vector<DeviceButtons::ButtonSpec> DeviceButtons::RawTapButtons(std::span<const RawTapButton> buttons,
                                                                    std::function<void(uint16_t, uint16_t)> tap) const {
    std::vector<ButtonSpec> out;
    for (const RawTapButton& b : buttons)
        out.push_back(ButtonSpec{b.id, b.name, [tap, x = b.x, y = b.y] { tap(x, y); }});
    return out;
}

}
