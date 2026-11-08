#include "ibm_workpad_z50_member_straps.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "ibm_workpad_z50_france_dram_32mb_id.h"
#include "ibm_workpad_z50_france_id.h"
#include "ibm_workpad_z50_germany_dram_32mb_id.h"
#include "ibm_workpad_z50_germany_id.h"
#include "ibm_workpad_z50_uk_dram_32mb_id.h"
#include "ibm_workpad_z50_uk_id.h"

namespace cerfcore {

namespace {

class IbmWorkpadZ50EmeaMemberStraps
    : public IbmWorkpadZ50FixedMemberStraps<0x0u, false, kIbmWorkpadZ50RomCard20Mb> {
public:
    using IbmWorkpadZ50FixedMemberStraps::IbmWorkpadZ50FixedMemberStraps;

    bool ShouldRegister() override {
        const std::string_view board = emu_.Get<BoardContext>().GetBoardId();
        return board == BoardId::IbmWorkpadZ50Uk || board == BoardId::IbmWorkpadZ50France ||
               board == BoardId::IbmWorkpadZ50Germany || board == BoardId::IbmWorkpadZ50UkDram32mb ||
               board == BoardId::IbmWorkpadZ50FranceDram32mb ||
               board == BoardId::IbmWorkpadZ50GermanyDram32mb;
    }
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50EmeaMemberStraps, IbmWorkpadZ50MemberStraps);

}
