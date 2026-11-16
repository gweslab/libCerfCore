#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/micrel_ksz9021/micrel_ksz9021_wiring.h"

#include <cstdint>
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kPhyAddress = 0u;

class SiemensSimaticMobilePanelsGen2Ksz9021Wiring final : public MicrelKsz9021Wiring {
public:
    using MicrelKsz9021Wiring::MicrelKsz9021Wiring;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }

    uint32_t PhyAddress() const override { return kPhyAddress; }
};

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2Ksz9021Wiring, MicrelKsz9021Wiring);

}
