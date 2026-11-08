#include "ibm_workpad_z50_member_straps.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "ibm_workpad_z50_dram_32mb_id.h"
#include "ibm_workpad_z50_id.h"

namespace cerfcore {

namespace {

class IbmWorkpadZ50UsMemberStraps
    : public IbmWorkpadZ50FixedMemberStraps<0x2u, true, kIbmWorkpadZ50RomCard20Mb> {
public:
    using IbmWorkpadZ50FixedMemberStraps::IbmWorkpadZ50FixedMemberStraps;

    bool ShouldRegister() override {
        const std::string_view board = emu_.Get<BoardContext>().GetBoardId();
        return board == BoardId::IbmWorkpadZ50 || board == BoardId::IbmWorkpadZ50Dram32mb;
    }
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50UsMemberStraps, IbmWorkpadZ50MemberStraps);

}
