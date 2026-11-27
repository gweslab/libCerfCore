#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../core/board_parts.h"
#include "../../peripherals/nxp_isp1504/isp1504_ulpi_phy.h"
#include "../../peripherals/nxp_isp1504/nxp_isp1504_part_id.h"
#include "../../socs/chipidea_ulpi_phys.h"
#include "toshiba_gigabeat_s_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kOtgCore = 0u;

class ToshibaGigabeatSUsbPhy : public ChipIdeaUlpiPhys {
public:
    using ChipIdeaUlpiPhys::ChipIdeaUlpiPhys;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS &&
               emu_.Get<BoardParts>().Has(PartId::NxpIsp1504);
    }

    void OnReady() override {
        phy_.PowerOn();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) phy_.PowerOn();
        });
    }

    UlpiPhy* Phy(uint32_t core) override { return core == kOtgCore ? &phy_ : nullptr; }

private:
    Isp1504UlpiPhy phy_;
};

}

REGISTER_SERVICE_AS(ToshibaGigabeatSUsbPhy, ChipIdeaUlpiPhys);

}
