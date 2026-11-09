#include "../../peripherals/national_pc97338/national_pc97338_board.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "compaq_aero_1500_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kDebugConnectorIsaIoBase = 0x16000000u;

class CompaqAero1500Pc97338Board : public NationalPc97338Board {
public:
    using NationalPc97338Board::NationalPc97338Board;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    uint32_t IsaIoBase() const override { return kDebugConnectorIsaIoBase; }
};

}

REGISTER_SERVICE_AS(CompaqAero1500Pc97338Board, NationalPc97338Board);

}
