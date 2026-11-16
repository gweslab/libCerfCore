#include "../../boot/board_boot_placer.h"
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mobile_panels_gen2_board_profile.h"
#include "siemens_simatic_mobile_panels_gen2_boot_handoff.h"
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

class SiemensSimaticMobilePanelsGen2Placer : public BoardBootPlacer {
public:
    using BoardBootPlacer::BoardBootPlacer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }

    void OnReady() override {
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { PlaceAfterRom(); });
    }

    void PlaceAfterRom() override {
        const auto& profile = SiemensSimaticMobilePanelsGen2BoardProfileFor(emu_.Get<BoardContext>().GetBoardId());
        const SiemensSimaticMobilePanelsGen2OalLayout oal = {"SiemensSimaticMobilePanelsGen2BootPlacer", profile.op_type,
                                        ResolveSiemensSimaticMobilePanelsGen2Panel(emu_.Get<BoardDatabase>().FindBoard(profile.board), profile)};
        emu_.Get<SiemensSimaticMobilePanelsGen2BootHandoff>().Place(oal);
    }
};

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2Placer, BoardBootPlacer);

}
