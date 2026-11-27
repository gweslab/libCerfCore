#include "../mips/mips_cp0_field_limits.h"

#include <string_view>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vr4102/vr4102_id.h"
#include "../../socs/vr4111/vr4111_id.h"
#include "../../socs/vr4121/vr4121_id.h"
#include "../../socs/vr4122/vr4122_id.h"

namespace cerfcore {

namespace {

constexpr MipsFixedFields kConfig{0x0F800000u, 0x00000000u};
constexpr MipsFixedFields kStatus{0x02000000u, 0x00000000u};

class Vr41xxCp0FieldLimits : public MipsCp0FieldLimits {
public:
    using MipsCp0FieldLimits::MipsCp0FieldLimits;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Vr4102 || soc == SocId::Vr4111 || soc == SocId::Vr4121 || soc == SocId::Vr4122;
    }

    MipsFixedFields Config() const override { return kConfig; }
    MipsFixedFields Status() const override { return kStatus; }
};

}

REGISTER_SERVICE_AS(Vr41xxCp0FieldLimits, MipsCp0FieldLimits);

}
