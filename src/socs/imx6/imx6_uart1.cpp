#include <cerfcore/core/cerf_emulator.h>


#include "imx6_uart.h"
#include "imx6_gic.h"

namespace cerfcore {

namespace {

class Imx6Uart1 : public Imx6Uart<0x02020000u, 1> {
    using Imx6Uart::Imx6Uart;

protected:
    void DriveIrq(bool level) override {
        if (level) emu_.Get<Imx6Gic>().AssertSpi(26);
        else       emu_.Get<Imx6Gic>().DeAssertSpi(26);
    }
};

}

REGISTER_SERVICE(Imx6Uart1);

}
