#include "../../socs/sh77xx/sh77xx_analog_inputs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "hp_600_family_id.h"
#include "hp_600_sh7709_battery.h"

namespace cerfcore {

namespace {

constexpr uint32_t kMainBatteryChannel = 3u;
constexpr uint32_t kBackupCellChannel  = 4u;
constexpr uint16_t kBackupCellAdc      = 0x3A3u;

class Hp600Sh7709AnalogInputs : public Sh77xxAnalogInputs {
public:
    using Sh77xxAnalogInputs::Sh77xxAnalogInputs;

    bool ShouldRegister() override {
        BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    void OnReady() override { battery_ = &emu_.Get<Hp600Sh7709Battery>(); }

    uint16_t Convert(uint32_t channel) override {
        if (channel == kMainBatteryChannel) return battery_->MainBatteryAdc();
        if (channel == kBackupCellChannel) return kBackupCellAdc;
        emu_.Get<Fatal>().Die("Hp600Sh7709AnalogInputs: A/D conversion of AN%u", channel);
    }

private:
    Hp600Sh7709Battery* battery_ = nullptr;
};

}

REGISTER_SERVICE_AS(Hp600Sh7709AnalogInputs, Sh77xxAnalogInputs);

}
