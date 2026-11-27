#include "../mips/mips_cp0_field_limits.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "vr5500_id.h"

namespace cerfcore {

namespace {

constexpr MipsFixedFields kConfig{0x00C00000u, 0x00000000u};
constexpr MipsFixedFields kStatus{0x00000000u, 0x00000000u};

class Vr5500Cp0FieldLimits : public MipsCp0FieldLimits {
public:
    using MipsCp0FieldLimits::MipsCp0FieldLimits;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetSocId() == SocId::Vr5500; }

    MipsFixedFields Config() const override { return kConfig; }
    MipsFixedFields Status() const override { return kStatus; }
};

}

REGISTER_SERVICE_AS(Vr5500Cp0FieldLimits, MipsCp0FieldLimits);

}
