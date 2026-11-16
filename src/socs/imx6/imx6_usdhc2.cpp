#include "imx6_usdhc_port.h"

namespace cerfcore {

namespace {
class Imx6Usdhc2 final : public Imx6UsdhcPort<0x02194000u, 23, 2u> {
public:
    using Imx6UsdhcPort::Imx6UsdhcPort;

};
}
REGISTER_SERVICE(Imx6Usdhc2);

}
