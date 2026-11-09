#include "../../socs/vr4121/vr4121_bcu_board.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vr41xx/vr41xx_bcu_regs.h"
#include "../../socs/vr4121/vr4121_bcu_regs.h"
#include <cerfcore/boards/board_context.h>
#include "../../core/board_config.h"
#include "../dram_card_param.h"
#include "ibm_workpad_z50_dram_layout.h"
#include "ibm_workpad_z50_family_id.h"

#include <algorithm>
#include <cstdint>

namespace cerfcore {

namespace {

uint32_t ExpansionBankBytes(uint32_t card_bytes, uint32_t index) {
    const uint32_t below = index * kIbmWorkpadZ50DramBankBytes;
    return card_bytes > below ? std::min(card_bytes - below, kIbmWorkpadZ50DramBankBytes) : 0u;
}

class IbmWorkpadZ50Vr4121BcuBoard : public Vr4121BcuBoard {
public:
    using Vr4121BcuBoard::Vr4121BcuBoard;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
    }

    bool Sdram() const override { return false; }

    std::optional<Vr4121DramWiring> DramWiring() const override {
        const uint32_t card = emu_.Get<BoardConfig>().Enum(BoardConfigParamId::DramCard, kDramCardBytes);
        return Vr4121DramWiring{ true, { kIbmWorkpadZ50DramBankBytes, 0u, ExpansionBankBytes(card, 0u),
                                         ExpansionBankBytes(card, 1u) } };
    }

    std::vector<Vr41xxBcuBootWrite> KernelEntryWrites() const override {
        return {
            { vr41xx_bcu::kOffCnt1, vr4121_bcu::kCnt1Rom64 | vr4121_bcu::kCnt1Dram64 },
        };
    }

    std::optional<Vr4121Revision> Revision() const override { return Vr4121Revision{ 0u, 0u }; }
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50Vr4121BcuBoard, Vr4121BcuBoard);

}
