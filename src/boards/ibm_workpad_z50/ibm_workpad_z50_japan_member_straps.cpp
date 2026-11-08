#include "ibm_workpad_z50_member_straps.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "ibm_workpad_z50_japan_dram_32mb_id.h"
#include "ibm_workpad_z50_japan_id.h"

namespace cerfcore {

namespace {

class IbmWorkpadZ50JapanMemberStraps
    : public IbmWorkpadZ50FixedMemberStraps<0x0u, true, kIbmWorkpadZ50RomCard32Mb> {
public:
    using IbmWorkpadZ50FixedMemberStraps::IbmWorkpadZ50FixedMemberStraps;

    bool ShouldRegister() override {
        const std::string_view board = emu_.Get<BoardContext>().GetBoardId();
        return board == BoardId::IbmWorkpadZ50Japan || board == BoardId::IbmWorkpadZ50JapanDram32mb;
    }
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50JapanMemberStraps, IbmWorkpadZ50MemberStraps);

}
