#include "imx6_pwm.h"

namespace cerfcore {

namespace {
class Imx6Pwm1 final : public Imx6Pwm<0x02080000u, FreescaleModule::kPwm1> {
public:
    using Imx6Pwm::Imx6Pwm;

};
}
REGISTER_SERVICE(Imx6Pwm1);

}
