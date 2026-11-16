#include "imx6_usb_phy.h"

namespace cerfcore {

namespace {
class Imx6UsbPhy0 final : public Imx6UsbPhy<0x020C9000u> {
public:
    using Imx6UsbPhy::Imx6UsbPhy;

};
}
REGISTER_SERVICE(Imx6UsbPhy0);

}
