#include "imx6_usb_phy.h"

namespace cerfcore {

namespace {
class Imx6UsbPhy1 final : public Imx6UsbPhy<0x020CA000u> {
public:
    using Imx6UsbPhy::Imx6UsbPhy;

};
}
REGISTER_SERVICE(Imx6UsbPhy1);

}
