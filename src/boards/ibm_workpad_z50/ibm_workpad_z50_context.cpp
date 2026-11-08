#include <cerfcore/boards/board_context.h>

#include "ibm_workpad_z50_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class IbmWorkpadZ50Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::IbmWorkpadZ50; }

    uint32_t GuestAdditionsWindowBase() const override { return 0x04000000u; }
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50Context, BoardContext);

}
