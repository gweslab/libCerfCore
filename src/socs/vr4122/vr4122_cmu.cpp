#include "../vr41xx/vr41xx_cmu.h"
#include "../reg_window_impl.h"

#include <cstdint>
#include "vr4122_id.h"

namespace cerfcore {

namespace {

using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegWindowBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

/* VR4131 UM Table 3-6 p82: CMU decodes 0x0F000060-0x0F00007F.
   VR4122 CMUCLKMSK, 0x0F000060 (NetBSD vripreg.h VR4122_CMU_ADDR): R/W bits
   13,12,11,10,8,7,6,4,1 (VR4131 UM 10.2.1; NetBSD cmureg.h VR4122_CMUMSK*). */
constexpr RegWindowModel kModel = {
    /*base=*/0x0F000060u,
    /*size=*/0x20u,
    /*num_regs=*/1u,
    /*word_pairs=*/false,
    {
        { ReadKind::kStored, WriteKind::kStored, 0x3DD2u, 0x0000u },
    },
};

class Vr4122Cmu : public RegWindowBase<SocId::Vr4122, kModel, Vr41xxCmu> {
public:
    using RegWindowBase::RegWindowBase;

protected:
    uint16_t ClockMask() const override { return StoredReg(0); }
    void BeforeStore(uint32_t, uint16_t value) override { ClockMaskWriting(value); }
};

}

REGISTER_SERVICE_AS(Vr4122Cmu, Vr41xxCmu);

}
