#include <cerfcore/boards/board_context.h>

#include "compaq_aero_1500_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class CompaqAero1500Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::CompaqAero1500; }

    uint32_t GuestAdditionsWindowBase() const override { return 0x0D000000u; }
};

}

REGISTER_SERVICE_AS(CompaqAero1500Context, BoardContext);

}
