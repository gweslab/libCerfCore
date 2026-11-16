#include "imx6_pwm.h"

namespace cerfcore {

namespace {
class Imx6Pwm3 final : public Imx6Pwm<0x02088000u, FreescaleModule::kPwm3> {
public:
    using Imx6Pwm::Imx6Pwm;

};
}
REGISTER_SERVICE(Imx6Pwm3);

}
