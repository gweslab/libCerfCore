#include "compaq_ipaq_h3000_ir_power.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "compaq_ipaq_h3000_egpio.h"
#include "compaq_ipaq_h3000_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint16_t kIrOn = 1u << 9;

class CompaqIpaqH3000EgpioIrPower : public CompaqIpaqH3000IrPower {
public:
    using CompaqIpaqH3000IrPower::CompaqIpaqH3000IrPower;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
    }

    bool Powered() const override {
        return (emu_.Get<CompaqIpaqH3000Egpio>().Latched() & kIrOn) != 0u;
    }
};

}

REGISTER_SERVICE_AS_FALLBACK(CompaqIpaqH3000EgpioIrPower, CompaqIpaqH3000IrPower);

}
