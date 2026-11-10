#include "../sh77xx/sh77xx_bsc_impl.h"
#include "sh7709_id.h"

namespace cerfcore {

namespace {

constexpr sh77xx_bsc::Model kSh7709Bsc{
    .bcr1_writable           = 0x37FFu,
    .wcr1_writable           = 0x3FF3u,
    .mcr_writable            = 0xFF5Fu,
    .pcr_writable            = 0x00FFu,
    .pcr_write_zero          = 0x0000u,
    .tpc_high_codes_reserved = true,
};

constexpr sh77xx_bsc::RegWindowModel kSh7709BscWindow = sh77xx_bsc::WindowOf(kSh7709Bsc);

class Sh7709Bsc : public sh77xx_bsc::Sh77xxBscBase<SocId::Sh7709, kSh7709BscWindow, kSh7709Bsc> {
public:
    using Sh77xxBscBase::Sh77xxBscBase;
};

}

REGISTER_SERVICE_AS(Sh7709Bsc, Sh77xxBsc);

}
