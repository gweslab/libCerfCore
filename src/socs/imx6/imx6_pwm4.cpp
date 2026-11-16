#include "imx6_pwm.h"

namespace cerfcore {

namespace {
class Imx6Pwm4 final : public Imx6Pwm<0x0208C000u, FreescaleModule::kPwm4> {
public:
    using Imx6Pwm::Imx6Pwm;

};
}
REGISTER_SERVICE(Imx6Pwm4);

}
