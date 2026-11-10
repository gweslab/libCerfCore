#include "../sh77xx/sh77xx_pfc_impl.h"
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr uint16_t kSh7709ControlReset[kSh77xxPortCount] = {
    0x0000u, 0x0000u, 0xAAAAu, 0xAA8Au, 0xAAAAu, 0xAAAAu, 0xAAAAu, 0xAAAAu, 0x0000u, 0x0000u, 0x0000u, 0xA888u,
};

constexpr cerf_reg_window_detail::RegWindowModel kSh7709PfcControl = sh77xx_pfc::ControlWindow(kSh7709ControlReset);

class Sh7709Pfc : public sh77xx_pfc::Sh77xxPfcBase<SocId::Sh7709, kSh7709PfcControl> {
public:
    using Sh77xxPfcBase::Sh77xxPfcBase;
};

}

REGISTER_SERVICE_AS(Sh7709Pfc, Sh77xxPfc);

}
