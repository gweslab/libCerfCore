#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/qualcomm_pm8058/pm8058_power.h"
#include "../../peripherals/qualcomm_pm8058/pm8058_power_key.h"
#include "../../socs/msm8255/msm8255_handset_remote_server.h"
#include "../../socs/msm8255/msm8255_system_power.h"
#include <cerfcore/boards/board_context.h>
#include "nokia_sea_ray_family_id.h"

namespace cerfcore {

namespace {

class NokiaSeaRaySystemPower : public Msm8255SystemPower {
public:
    using Msm8255SystemPower::Msm8255SystemPower;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override {
        emu_.Get<Pm8058PowerKey>().AddListener([this](bool down) {
            emu_.Get<Msm8255HandsetRemoteServer>().ReportPowerKey(down);
        });
    }

    void PowerDown() override { emu_.Get<Pm8058Power>().PowerOff(); }

    void ConfigurePowerKeyReset(uint32_t hold_ms, bool power_off) override {
        emu_.Get<Pm8058PowerKey>().ConfigureHardReset(hold_ms, power_off);
    }
};

}

REGISTER_SERVICE_AS(NokiaSeaRaySystemPower, Msm8255SystemPower);

}
