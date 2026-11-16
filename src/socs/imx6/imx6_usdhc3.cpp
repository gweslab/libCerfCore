#include "imx6_usdhc_port.h"

namespace cerfcore {

namespace {
class Imx6Usdhc3 final : public Imx6UsdhcPort<0x02198000u, 24, 3u> {
public:
    using Imx6UsdhcPort::Imx6UsdhcPort;

};
}
REGISTER_SERVICE(Imx6Usdhc3);

}
