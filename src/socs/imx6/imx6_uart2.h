#pragma once

#include "imx6_uart.h"
#include "imx6_gic.h"

namespace cerfcore {

class Imx6Uart2 : public Imx6Uart<0x021E8000u, 2> {
public:
    using Imx6Uart::Imx6Uart;

protected:
    void DriveIrq(bool level) override {
        if (level) emu_.Get<Imx6Gic>().AssertSpi(27);
        else       emu_.Get<Imx6Gic>().DeAssertSpi(27);
    }
};

}
