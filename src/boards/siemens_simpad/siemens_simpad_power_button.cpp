#include "siemens_simpad_power_button.h"

#include "siemens_simpad_family_id.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/state/hibernation.h>

namespace cerfcore {

namespace {

/* Linux arch/arm/mach-sa1100 simpad.h GPIO_POWER_BUTTON = GPIO_GPIO0; simpad.c
   simpad_button_table: KEY_POWER, active low; simpad_power_off arms PWER_GPIO0. */
constexpr uint32_t kGpioPowerButton = 0;

}

REGISTER_SERVICE(SiemensSimpadPowerButton);

bool SiemensSimpadPowerButton::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
}

void SiemensSimpadPowerButton::OnReady() {
    SetPressed(false);
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] { SetPressed(false); });
    emu_.Get<GuestDeepSleep>().RegisterUserWakeInput([this] {
        SetPressed(true);
        SetPressed(false);
    });
}

void SiemensSimpadPowerButton::SetPressed(bool pressed) {
    emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioPowerButton, !pressed);
}

}
