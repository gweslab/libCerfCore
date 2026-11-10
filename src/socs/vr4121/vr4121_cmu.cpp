#include "../vr41xx/vr41xx_cmu.h"
#include "../reg_window_impl.h"

#include <cstdint>
#include "vr4121_id.h"

namespace cerfcore {

namespace {

using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegWindowBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

/* VR4121 UM Table 6-12 p178: CMU decodes 0x0B000060-0x0B00007F.
   VR4121 CMUCLKMSK, 0x0B000060 (VR4121 UM Table 14-1). D10 MSKFFIR, D9 MSKSHSP,
   D8 MSKSSIU, D5 MSKDSIU, D4 MSKFIR, D3 MSKKIU, D2 MSKAIU, D1 MSKSIU, D0 MSKPIU
   are R/W; D15:11 and D7:6 reserved; both reset rows 0 (VR4121 UM 14.2.1). */
constexpr RegWindowModel kModel = {
    /*base=*/0x0B000060u,
    /*size=*/0x20u,
    /*num_regs=*/1u,
    /*word_pairs=*/false,
    {
        { ReadKind::kStored, WriteKind::kStored, 0x073Fu, 0x0000u },
    },
};

class Vr4121Cmu : public RegWindowBase<SocId::Vr4121, kModel, Vr41xxCmu> {
public:
    using RegWindowBase::RegWindowBase;

protected:
    uint16_t ClockMask() const override { return StoredReg(0); }
    void BeforeStore(uint32_t, uint16_t value) override { ClockMaskWriting(value); }
};

}

REGISTER_SERVICE_AS(Vr4121Cmu, Vr41xxCmu);

}
