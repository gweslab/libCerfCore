#include "../mips/mips_cp0_field_limits.h"

#include <string_view>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/pr31x00/pr31500_id.h"
#include "../../socs/pr31x00/pr31700_id.h"

namespace cerfcore {

namespace {

constexpr MipsFixedFields kConfig{0x00000000u, 0x00000000u};
constexpr MipsFixedFields kStatus{0x02000000u, 0x00000000u};

class Tx39Cp0FieldLimits : public MipsCp0FieldLimits {
public:
    using MipsCp0FieldLimits::MipsCp0FieldLimits;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Pr31500 || soc == SocId::Pr31700;
    }

    MipsFixedFields Config() const override { return kConfig; }
    MipsFixedFields Status() const override { return kStatus; }
};

}

REGISTER_SERVICE_AS(Tx39Cp0FieldLimits, MipsCp0FieldLimits);

}
