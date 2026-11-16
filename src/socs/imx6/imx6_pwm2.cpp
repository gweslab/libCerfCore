#include "imx6_pwm.h"

namespace cerfcore {

namespace {
class Imx6Pwm2 final : public Imx6Pwm<0x02084000u, FreescaleModule::kPwm2> {
public:
    using Imx6Pwm::Imx6Pwm;

};
}
REGISTER_SERVICE(Imx6Pwm2);

}
