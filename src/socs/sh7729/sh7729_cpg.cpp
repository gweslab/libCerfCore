#include "../sh77xx/sh77xx_cpg_impl.h"
#include "sh7729_id.h"

namespace cerfcore {

namespace {

constexpr Sh77xxFrqcrSetting kSh7729Modes34[] = {
    {0x0100u, 1u, 1u, 0u}, {0x0101u, 1u, 2u, 0u}, {0x0102u, 1u, 4u, 0u}, {0x01D0u, 2u, 1u, 2u},
    {0x01D1u, 2u, 2u, 2u}, {0x01D2u, 2u, 4u, 2u}, {0x01D4u, 1u, 1u, 2u}, {0x01D5u, 1u, 2u, 2u},
    {0x01D6u, 1u, 4u, 2u}, {0x81C0u, 3u, 1u, 3u}, {0x81C1u, 3u, 2u, 3u}, {0xC1C0u, 1u, 1u, 3u},
    {0xC1C1u, 1u, 2u, 3u}, {0x01E0u, 4u, 1u, 4u}, {0x01E1u, 4u, 2u, 4u}, {0x01E4u, 2u, 1u, 4u},
    {0x01E5u, 2u, 2u, 4u}, {0x01E6u, 2u, 4u, 4u}, {0x01E8u, 1u, 1u, 4u}, {0x01E9u, 1u, 2u, 4u},
    {0x01EAu, 1u, 4u, 4u}, {0x01F0u, 8u, 1u, 8u}, {0x81D0u, 6u, 1u, 6u},
};

constexpr Sh77xxCpgModel kSh7729Cpg{kSh7729Modes34, static_cast<uint32_t>(std::size(kSh7729Modes34))};

class Sh7729Cpg : public Sh77xxCpgBase<SocId::Sh7729, kSh7729Cpg> {
public:
    using Sh77xxCpgBase::Sh77xxCpgBase;
};

}

REGISTER_SERVICE_AS(Sh7729Cpg, Sh77xxCpg);

}
