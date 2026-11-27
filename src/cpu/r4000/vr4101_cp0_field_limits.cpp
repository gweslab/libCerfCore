#include "../mips/mips_cp0_field_limits.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vr4101/vr4101_id.h"

namespace cerfcore {

namespace {

constexpr MipsFixedFields kConfig{0x0F800000u, 0x03000000u};
constexpr MipsFixedFields kStatus{0x02000000u, 0x00000000u};

class Vr4101Cp0FieldLimits : public MipsCp0FieldLimits {
public:
    using MipsCp0FieldLimits::MipsCp0FieldLimits;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetSocId() == SocId::Vr4101; }

    MipsFixedFields Config() const override { return kConfig; }
    MipsFixedFields Status() const override { return kStatus; }
};

}

REGISTER_SERVICE_AS(Vr4101Cp0FieldLimits, MipsCp0FieldLimits);

}
