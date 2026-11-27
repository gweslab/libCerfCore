#include "../../boot/oal_args_header.h"
#include "nokia_sea_ray_family_id.h"
#include "nokia_sea_ray_oal_args_layout.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint16_t kOalVersion = 1u;
constexpr uint16_t kBspVersion = 17u;

class NokiaSeaRayOalArgs : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override {
        WriteHeader();
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { WriteHeader(); });
    }

private:
    void WriteHeader() {
        emu_.Get<OalArgsHeader>().Write(kNokiaSeaRayArgsPa, kOalVersion, kBspVersion);
        LOG(Board, "NokiaSeaRayOalArgs: header at PA 0x%08X, OAL %u, BSP %u\n",
            kNokiaSeaRayArgsPa, kOalVersion, kBspVersion);
    }
};

}

REGISTER_SERVICE(NokiaSeaRayOalArgs);

}
