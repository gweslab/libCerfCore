#include "board_not_found_service.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/host/configuration_problem.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>

#include <string>

namespace cerfcore {

REGISTER_SERVICE(BoardNotFoundService);

bool BoardNotFoundService::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardId().empty();
}

void BoardNotFoundService::EnsureFound() {
    const std::string& id = emu_.Get<DeviceConfig>().board_id;

#if !CERF_DEV_MODE
    emu_.Get<ConfigurationProblem>().BoardUnsupported(id);
#endif

    if (id.empty())
        emu_.Get<Fatal>().UserError("no board selected: set cerf.json board.id or pass "
                                    "--board-id=ID (run --help for the id list)");
    emu_.Get<Fatal>().UserError("unrecognised board id '%s': run --help for the "
                                "supported id list", id.c_str());
}

}
