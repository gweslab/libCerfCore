#include "sa11xx_gpio_clock_input.h"

namespace cerfcore {

void Sa11xxGpioClockInput::RegisterChangeListener(std::function<void()> fn) {
    listeners_.Add(std::move(fn));
}

void Sa11xxGpioClockInput::NotifyChange() {
    listeners_.Call();
}

}
