#include "nec_mobilepro_700_battery.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/nec_vrc4172/vrc4172_gpio.h"
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

namespace cerfcore {

namespace {

constexpr int kOffAcPin = 5;

}

bool NecMobilePro700Battery::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
}

void NecMobilePro700Battery::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
    battery_.SetChangeHandler([this] { DriveAcPin(); });
    DriveAcPin();
}

void NecMobilePro700Battery::DriveAcPin() {
    emu_.Get<Vrc4172Gpio>().SetPinLevel(kOffAcPin, battery_.IsOnBattery());
}

REGISTER_SERVICE(NecMobilePro700Battery);

}
