#include <cerfcore/boards/board_context.h>

#include "casio_jx670_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class CasioJx670Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::CasioJx670; }

    /* VR4131 UM Fig 3-1: PA 0x20000000-0xFFFFFFFF mirrors 0x00000000-0x1FFFFFFF. */
    uint32_t GuestAdditionsWindowBase() const override { return 0x04000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioJx670Context, BoardContext);

}
