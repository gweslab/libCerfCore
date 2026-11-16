#pragma once

#include "../freescale_uart_impl.h"
#include "imx6_id.h"

namespace cerfcore {

template <uint32_t kBase, int kUartNum>
class Imx6Uart : public cerf_freescale_uart_detail::FreescaleUartBase<kBase, kUartNum, SocId::Imx6dq> {
public:
    using Parent = cerf_freescale_uart_detail::FreescaleUartBase<kBase, kUartNum, SocId::Imx6dq>;
    using Parent::Parent;

    bool ShouldRegister() override {
        auto* bd = this->emu_.template TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
};

}
