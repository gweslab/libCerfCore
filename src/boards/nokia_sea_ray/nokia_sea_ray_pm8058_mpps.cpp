#include "../../peripherals/qualcomm_pm8058/pm8058_irq.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nokia_sea_ray_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kMppIrqBlock    = 16u;
constexpr uint32_t kMppCount       = 12u;
constexpr uint32_t kFlipSwitchMpp  = 5u;

class NokiaSeaRayPm8058Mpps : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override {
        auto& irq = emu_.Get<Pm8058Irq>();
        for (uint32_t mpp = 0; mpp < kMppCount; ++mpp) {
            if (mpp == kFlipSwitchMpp) continue;
            irq.GuardUnmodeledSource(kMppIrqBlock * Pm8058Irq::kIrqsPerBlock + mpp);
        }
    }
};

}

REGISTER_SERVICE(NokiaSeaRayPm8058Mpps);

}
