#include "../vr41xx/vr41xx_dcu_impl.h"

#include <cstdint>
#include "vr4121_id.h"

namespace cerfcore {

namespace {

using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

constexpr ReadKind  kRd = ReadKind::kStored;
constexpr WriteKind kWr = WriteKind::kStored;

constexpr RegWindowModel kModel = {
    /*base=*/0x0B000040u,
    /*size=*/0x20u,
    /*num_regs=*/6u,
    /*word_pairs=*/false,
    {
        { kRd, kWr, 0x0001u, 0x0000u },   /* 0x40 DMARSTREG  D0 DMARST 0=reset 1=normal (13.3.1) */
        {},                               /* 0x42 DMAIDLEREG D0 DMAISTAT 1=idle 0=busy, R (13.3.2) */
        { kRd, kWr, 0x0001u, 0x0000u },   /* 0x44 DMASENREG  D0 DMASEN sequencer enable (13.3.3) */
        { kRd, kWr, 0x000Du, 0x0000u },   /* 0x46 DMAMSKREG  D3 DMAMSKAIN D2 DMAMSKAOUT D0 DMAMSKFOUT (13.3.4) */
        { ReadKind::kZero, WriteKind::kDrop, 0x0000u, 0x0000u },
                                          /* 0x48 DMAREQREG  D3/D2/D0 request pending, R (13.3.5) */
        { kRd, kWr, 0x0001u, 0x0000u },   /* 0x4A TDREG      D0 FIR transfer direction (13.3.6) */
    },
};

class Vr4121Dcu : public Vr41xxDcuImpl<SocId::Vr4121, kModel> {
public:
    using Vr41xxDcuImpl::Vr41xxDcuImpl;
};

}

REGISTER_SERVICE_AS(Vr4121Dcu, Vr41xxDcu);

}
