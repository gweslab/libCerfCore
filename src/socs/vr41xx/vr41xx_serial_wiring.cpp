#include "vr41xx_serial_wiring.h"

#include <cerfcore/core/cerf_emulator.h>
#include "vr41xx_giu.h"

#include <utility>

namespace cerfcore {

Vr41xxSerialModem::Vr41xxSerialModem(std::string label_in, int carrier_gpio_in)
    : label(std::move(label_in)), carrier_gpio(carrier_gpio_in) {}

bool Vr41xxGpioSuppliedSerialWiring::SupplyOn() {
    return emu_.Get<Vr41xxGiu>().OutputDrive(SupplyGpio()) == PinDrive::kHigh;
}

void Vr41xxGpioSuppliedSerialWiring::RegisterSupplyListener(std::function<void()> fn) {
    emu_.Get<Vr41xxGiu>().RegisterOutputDriveListener(std::move(fn));
}

}
