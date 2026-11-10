#include "compaq_aero_1500_battery.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/nec_vrc4171a/nec_vrc4171a.h"
#include "../../peripherals/nec_vrc4171a/nec_vrc4171a_lcd.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "compaq_aero_1500_family_id.h"

#include <cstddef>

namespace cerfcore {

namespace {

constexpr int      kAcGpio             = 3;
constexpr int      kBatteryPresentGpio = 23;
constexpr int      kChargerPowerGpio   = 24;
constexpr int      kChargerEnableGpio  = 40;
constexpr uint16_t kFallPerConversion  = 2u;

struct FillWord {
    uint16_t word;
    int      fill;
};

constexpr FillWord kRestVoltage[] = {
    {0x390u, 100}, {0x385u, 90}, {0x37Cu, 80}, {0x374u, 70}, {0x36Eu, 60},
    {0x365u, 50},  {0x35Au, 40}, {0x350u, 30}, {0x346u, 20}, {0x338u, 0},
};

constexpr FillWord kLoadedRestVoltage[] = {
    {0x387u, 100}, {0x37Cu, 90}, {0x372u, 80}, {0x36Bu, 70}, {0x365u, 60},
    {0x35Bu, 50},  {0x350u, 40}, {0x347u, 30}, {0x33Cu, 20}, {0x32Eu, 0},
};

constexpr FillWord kChargeVoltage[] = {
    {0x390u, 30}, {0x37Du, 20}, {0x370u, 0},
};

constexpr FillWord kChargeCurrent[] = {
    {0x008u, 100}, {0x033u, 90}, {0x07Cu, 80}, {0x0C2u, 70},
    {0x127u, 60},  {0x18Bu, 50}, {0x1C1u, 40}, {0x1E7u, 0},
};

template <size_t N>
uint16_t WordForFill(const FillWord (&rows)[N], int fill) {
    for (size_t i = 0; i + 1u < N; ++i) {
        if (fill >= rows[i].fill) return rows[i].word;
    }
    return rows[N - 1u].word;
}

}

REGISTER_SERVICE(CompaqAero1500Battery);

bool CompaqAero1500Battery::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
}

void CompaqAero1500Battery::OnReady() {
    giu_     = &emu_.Get<Vr41xxGiu>();
    lcd_     = &emu_.Get<NecVrc4171aLcd>();
    vrc4171_ = &emu_.Get<NecVrc4171a>();
    giu_->RegisterPinDeviceState([this](StateWriter& w) { SaveState(w); },
                                 [this](StateReader& r) { RestoreState(r); });
    battery_.SetChangeHandler([this] { DrivePins(); });
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
    DrivePins();
}

bool CompaqAero1500Battery::Charging() {
    return !battery_.IsOnBattery() &&
           giu_->OutputDrive(kChargerEnableGpio) == PinDrive::kHigh;
}

uint16_t CompaqAero1500Battery::TargetVoltageWord() {
    const int fill = battery_.FillPercent();
    return Charging() ? WordForFill(kChargeVoltage, fill) : WordForFill(kRestVoltage, fill);
}

bool CompaqAero1500Battery::UnderLoad() {
    return lcd_->BacklightOn() && vrc4171_->SocketPowered(0);
}

uint16_t CompaqAero1500Battery::CellVoltageWord() {
    const uint16_t target = TargetVoltageWord();
    const bool     loaded = !Charging() && UnderLoad();
    const int      fill   = battery_.FillPercent();
    const uint16_t sag    = loaded ? static_cast<uint16_t>(WordForFill(kRestVoltage, fill) -
                                                           WordForFill(kLoadedRestVoltage, fill))
                                   : 0u;
    std::lock_guard<std::mutex> lk(mtx_);
    if (target >= voltage_word_ || voltage_word_ - target <= kFallPerConversion) {
        voltage_word_ = target;
    } else {
        voltage_word_ = static_cast<uint16_t>(voltage_word_ - kFallPerConversion);
    }
    return static_cast<uint16_t>(voltage_word_ - sag);
}

uint16_t CompaqAero1500Battery::ChargeCurrentWord() {
    return Charging() ? WordForFill(kChargeCurrent, battery_.FillPercent()) : 0u;
}

void CompaqAero1500Battery::DrivePins() {
    const bool on_battery = battery_.IsOnBattery();
    giu_->SetPinLevel(kAcGpio, on_battery);
    giu_->SetPinLevel(kBatteryPresentGpio, false);
    giu_->SetPinLevel(kChargerPowerGpio, !on_battery);
}

void CompaqAero1500Battery::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write("aero_batt_voltage", voltage_word_);
}

void CompaqAero1500Battery::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read("aero_batt_voltage", voltage_word_);
}

}
