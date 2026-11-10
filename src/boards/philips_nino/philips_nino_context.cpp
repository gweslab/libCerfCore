#include <cerfcore/boards/board_context.h>

#include "philips_nino_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/pr31x00/pr31x00_reserved_space.h"

namespace cerfcore {

namespace {

class PhilipsNinoContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::PhilipsNino; }

    uint32_t GuestAdditionsWindowBase() const override { return kPr31x00ReservedSpaceBase; }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsNinoContext, BoardContext);

}
