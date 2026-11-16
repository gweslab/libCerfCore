#include <cerfcore/boards/board_context.h>

#include <cerfcore/core/cerf_emulator.h>
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

class SiemensSimaticMobilePanelsGen2Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override {
        return BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }
};

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2Context, BoardContext);

}
