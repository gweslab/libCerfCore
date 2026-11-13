#include "ibm_workpad_z50_battery.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "ibm_workpad_z50_family_id.h"

#include <array>
#include <cstddef>
#include <utility>

namespace cerfcore {

namespace {

struct CurvePoint {
    int      fill;
    uint16_t mv;
};

constexpr int      kLatchOrLowGpio     = 0;
constexpr int      kLiIonPackGpio      = 7;
constexpr int      kChargerDisableGpio = 10;
constexpr uint16_t kDesignCapacityMah  = 1500u;
constexpr uint16_t kFullChargeMv       = 12600u;
constexpr int      kBatteryLowPercent  = 3;
constexpr uint16_t kEdv1Mv             = 9000u;
constexpr uint16_t kEdvfMv             = 8400u;
constexpr int16_t  kChargeMa           = 1000;
constexpr int16_t  kDischargeMa        = -188;

constexpr std::array<CurvePoint, 21> kDischargeCurve = {{
    {0, kEdvfMv},   {kBatteryLowPercent, kEdv1Mv}, {4, 9562},   {5, 9972},   {6, 10258},
    {7, 10463},     {8, 10582},   {10, 10677},  {13, 10775},  {17, 10875},  {23, 10976},
    {32, 11074},    {45, 11163},  {49, 11204},  {55, 11293},  {67, 11484},  {70, 11552},
    {85, 11911},    {96, 12253},  {99, 12363},  {100, kFullChargeMv},
}};

constexpr std::array<CurvePoint, 18> kChargeCurve = {{
    {0, 10700},   {3, 10700},   {4, 10717},   {5, 11095},   {6, 11326},   {7, 11465},
    {8, 11517},   {18, 11733},  {27, 11849},  {47, 12055},  {56, 12180},  {63, 12296},
    {64, 12305},  {66, 12356},  {70, 12428},  {75, 12546},  {77, kFullChargeMv},
    {100, kFullChargeMv},
}};

template <size_t N>
uint16_t CurveMv(const std::array<CurvePoint, N>& curve, int fill) {
    size_t i = 1u;
    while (i < N - 1u && fill > curve[i].fill) ++i;
    const CurvePoint& lo = curve[i - 1u];
    const CurvePoint& hi = curve[i];
    return static_cast<uint16_t>(lo.mv + (hi.mv - lo.mv) * (fill - lo.fill) / (hi.fill - lo.fill));
}

}

REGISTER_SERVICE(IbmWorkpadZ50Battery);

bool IbmWorkpadZ50Battery::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
}

void IbmWorkpadZ50Battery::OnReady() {
    giu_ = &emu_.Get<Vr41xxGiu>();
    giu_->SetPinLevel(kLatchOrLowGpio, false);
    giu_->SetPinLevel(kLiIonPackGpio, false);
    giu_->RegisterOutputDriveListener([this] { NotifyChange(); });
    battery_.SetChangeHandler([this] { NotifyChange(); });
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
}

bool IbmWorkpadZ50Battery::OnAc() const { return !battery_.IsOnBattery(); }

int IbmWorkpadZ50Battery::FillPercent() const { return battery_.FillPercent(); }

bool IbmWorkpadZ50Battery::ChargerDisabled() const {
    return giu_->OutputDrive(kChargerDisableGpio) == PinDrive::kHigh;
}

bool IbmWorkpadZ50Battery::Charging() const {
    return OnAc() && !ChargerDisabled() && FillPercent() < 100;
}

bool IbmWorkpadZ50Battery::Discharging() const { return !OnAc() || ChargerDisabled(); }

bool IbmWorkpadZ50Battery::ChargeComplete() const {
    return OnAc() && !ChargerDisabled() && FillPercent() >= 100;
}

uint16_t IbmWorkpadZ50Battery::DesignCapacityMah() const { return kDesignCapacityMah; }

uint16_t IbmWorkpadZ50Battery::VoltageMv() const {
    return Charging() ? CurveMv(kChargeCurve, FillPercent())
                      : CurveMv(kDischargeCurve, FillPercent());
}

uint16_t IbmWorkpadZ50Battery::FullChargeMv() const { return kFullChargeMv; }

int16_t IbmWorkpadZ50Battery::CurrentMa() const {
    if (Charging()) return kChargeMa;
    if (Discharging()) return kDischargeMa;
    return 0;
}

uint16_t IbmWorkpadZ50Battery::Edv1Mv() const { return kEdv1Mv; }

uint16_t IbmWorkpadZ50Battery::EdvfMv() const { return kEdvfMv; }

void IbmWorkpadZ50Battery::RegisterChangeListener(std::function<void()> fn) {
    change_listeners_.Add(std::move(fn));
}

void IbmWorkpadZ50Battery::NotifyChange() {
    change_listeners_.Call();
}

}
