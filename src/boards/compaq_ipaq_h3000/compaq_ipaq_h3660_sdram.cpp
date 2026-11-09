#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../core/board_config.h"
#include "../../socs/sa11xx/sa1110_sdram_geometry.h"
#include "../ram_param_id.h"
#include "compaq_ipaq_h3000_family_id.h"

namespace cerfcore {

namespace {

/* handhelds.org bootldr 2.18.01 boot-sa1100.s dram_mdcnfg_64mb: MDCNFG_DRAC0(6), "Have to
   set DRAC0 to 15 row bits or else you only get 9 col bits". */
class CompaqIpaqH3660Sdram : public Sa1110SdramGeometry {
public:
    using Sa1110SdramGeometry::Sa1110SdramGeometry;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000 &&
               emu_.Get<BoardConfig>().EnumId(BoardConfigParamId::Ram) == "64mb";
    }

    uint32_t RowBits() const override    { return 13; }
    uint32_t ColumnBits() const override { return 9; }
    uint32_t BankBits() const override   { return 2; }
};

}

REGISTER_SERVICE_AS(CompaqIpaqH3660Sdram, Sa1110SdramGeometry);

}
