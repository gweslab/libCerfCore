#include "compaq_ipaq_h3000_gpio_buttons.h"

#include "compaq_ipaq_h3000_family_id.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/state/hibernation.h>

namespace cerfcore {

REGISTER_SERVICE(CompaqIpaqH3000GpioButtons);

bool CompaqIpaqH3000GpioButtons::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
}

void CompaqIpaqH3000GpioButtons::OnReady() {
    ReleaseAll();
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] { ReleaseAll(); });
    emu_.Get<GuestDeepSleep>().RegisterUserWakeInput([this] {
        SetPressed(kIpaqH3000PowerButtonPin, true);
        SetPressed(kIpaqH3000PowerButtonPin, false);
    });
}

void CompaqIpaqH3000GpioButtons::SetPressed(uint32_t pin, bool pressed) {
    emu_.Get<Sa11xxGpio>().DriveInputPin(pin, !pressed);
}

void CompaqIpaqH3000GpioButtons::ReleaseAll() {
    SetPressed(kIpaqH3000PowerButtonPin, false);
    SetPressed(kIpaqH3000ActionButtonPin, false);
}

}
