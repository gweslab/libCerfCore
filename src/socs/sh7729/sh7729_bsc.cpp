#include "../sh77xx/sh77xx_bsc_impl.h"
#include "sh7729_id.h"

namespace cerfcore {

namespace {

constexpr sh77xx_bsc::Model kSh7729Bsc{
    .bcr1_writable           = 0xF7FFu,
    .wcr1_writable           = 0xBFF3u,
    .mcr_writable            = 0xFFFFu,
    .pcr_writable            = 0xCFFFu,
    .pcr_write_zero          = 0x3000u,
    .tpc_high_codes_reserved = false,
};

constexpr sh77xx_bsc::RegWindowModel kSh7729BscWindow = sh77xx_bsc::WindowOf(kSh7729Bsc);

class Sh7729Bsc : public sh77xx_bsc::Sh77xxBscBase<SocId::Sh7729, kSh7729BscWindow, kSh7729Bsc> {
public:
    using Sh77xxBscBase::Sh77xxBscBase;
};

}

REGISTER_SERVICE_AS(Sh7729Bsc, Sh77xxBsc);

}
