#include "../../peripherals/open_bus_window.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nec_cronos3e_family_id.h"
#include "nec_mobilegear2_r430_id.h"
#include "nec_mobilegear2_r530_id.h"
#include "nec_mobilepro_780_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

class NecCronos3eNoInternalFlash : public OpenBusWindow {
public:
    using OpenBusWindow::OpenBusWindow;

    uint32_t MmioBase() const override { return 0x11C00000u; }
    uint32_t MmioSize() const override { return 4u; }

protected:
    std::string_view WindowBoardFamilyId() const override { return BoardFamilyId::NecCronos3e; }
    bool WindowOnBoard(const BoardContext& bd) const override {
        const auto board = bd.GetBoardId();
        return board == BoardId::NecMobilepro780 || board == BoardId::NecMobilegear2R530 ||
               board == BoardId::NecMobilegear2R430;
    }
    const char*      WindowTag()           const override { return "CRONOS3E no internal flash"; }
};

}

REGISTER_SERVICE(NecCronos3eNoInternalFlash);

}
