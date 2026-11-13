#include "hp_600_sh7709_battery.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "../../socs/sh77xx/sh77xx_intc.h"
#include "hp_600_family_id.h"

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(Hp600Sh7709Battery);

namespace {

constexpr uint16_t kLevelThresholds[] = {0x286u, 0x2C7u, 0x312u, 0x332u, 0x345u, 0x35Bu, 0x373u};
constexpr int      kTopLevel          = 6;
constexpr int      kPercentPerLevel   = 16;
constexpr int      kFullPercent       = 100;

}

bool Hp600Sh7709Battery::ShouldRegister() {
    BoardContext& board = emu_.Get<BoardContext>();
    return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
}

void Hp600Sh7709Battery::OnReady() {
    intc_ = &emu_.Get<Sh77xxIntc>();
    battery_.SetChangeHandler([this] { DriveAcLine(); });
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
    DriveAcLine();
}

bool Hp600Sh7709Battery::Charging() const {
    return !battery_.IsOnBattery() && battery_.FillPercent() < kFullPercent;
}

uint16_t Hp600Sh7709Battery::MainBatteryAdc() const {
    const int level = std::min((battery_.FillPercent() + kPercentPerLevel / 2) / kPercentPerLevel, kTopLevel);
    if (level == kTopLevel) return kLevelThresholds[kTopLevel];
    return static_cast<uint16_t>(kLevelThresholds[level] - 1u);
}

void Hp600Sh7709Battery::DriveAcLine() { intc_->SetPinLevel(Sh77xxIntcPin::kIrq1, battery_.IsOnBattery()); }

}
