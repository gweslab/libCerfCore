#include <cerfcore/boards/board_context.h>

#include "sharp_mobilon_hc_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class SharpMobilonHcContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::SharpMobilonHc; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHcContext, BoardContext);

}
