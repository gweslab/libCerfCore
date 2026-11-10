#include "../../peripherals/hitachi_hd64461/hd64461_gpio_wiring.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/hitachi_hd64461/hd64461_gpio.h"
#include "hp_600_family_id.h"
#include "hp_600_net_hold.h"

namespace cerfcore {

namespace {

class Hp600Hd64461GpioWiring : public Hd64461GpioWiring {
public:
    using Hd64461GpioWiring::Hd64461GpioWiring;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
    }

    void TrackDrive(const Hd64461Gpio& gpio) override { nets_.Track(gpio); }

    uint8_t NetLevels(uint32_t port) const override { return nets_.Levels(port); }

    void SaveState(StateWriter& w) override { nets_.SaveState(w); }

    void RestoreState(StateReader& r) override { nets_.RestoreState(r); }

private:
    Hp600NetHold<cerf_hd64461_gpio_detail::kPorts, cerf_hd64461_gpio_detail::kPins> nets_;
};

}

REGISTER_SERVICE_AS(Hp600Hd64461GpioWiring, Hd64461GpioWiring);

}
