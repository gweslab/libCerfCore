#include "ti_bq27520_monitor.h"

#include "../../core/board_parts.h"
#include "../../core/deferred_jobs.h"
#include "../battery_pack.h"
#include "ti_bq27520_flash.h"
#include "ti_bq27520_part_id.h"
#include "ti_bq27520_rom_mode.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>

#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kRegOpConfigLow       = 1u;
constexpr uint32_t kRegSocDelta          = 7u;
constexpr uint32_t kRegDfWrIndWaitTimeHi = 9u;
constexpr uint32_t kRegDfWrIndWaitTimeLo = 10u;
constexpr uint32_t kRegOpConfigB         = 11u;
constexpr uint32_t kRegOpConfigD         = 13u;

constexpr uint32_t kDischargeSysDownSet   = 5u;
constexpr uint32_t kDischargeSysDownClear = 8u;

constexpr uint8_t kOpConfigIntFocv    = 1u << 7;
constexpr uint8_t kOpConfigSociPol    = 1u << 3;
constexpr uint8_t kOpConfigBBie       = 1u << 6;
constexpr uint8_t kOpConfigBBlInt     = 1u << 5;
constexpr uint8_t kOpConfigBDfWrIndBl = 1u << 2;
constexpr uint8_t kOpConfigDSocState  = 1u << 5;
constexpr uint8_t kOpConfigDSocDfw    = 1u << 3;

constexpr int     kSocFull  = 100;
constexpr int     kSocEmpty = 0;
constexpr int64_t kSlotNs   = 1000000000;

}

bool TiBq27520Monitor::ShouldRegister() {
    return emu_.Get<BoardParts>().Has(PartId::TiBq27520);
}

void TiBq27520Monitor::OnReady() {
    battery_    = &emu_.Get<BatteryPack>();
    rom_        = &emu_.Get<TiBq27520RomMode>();
    clock_      = &emu_.Get<GuestCycleClock>();
    slot_event_ = clock_->Add([this] { DeliverSlotPulse(); });
    clock_->RegisterRateListener([this] {
        if (slot_pulse_armed_) ArmSlotPulse();
    });
    slot_origin_ns_ = clock_->NowNs();
    if (!rom_->Active()) Baseline();
    battery_->AddChangeListener([this] {
        emu_.Get<DeferredJobs>().PostToGuestThread([this] { OnBatteryChange(); });
    });
}

void TiBq27520Monitor::AddSocIntListener(CallbackList<bool>::Callback listener) {
    listener(true);
    soc_int_.Add(std::move(listener));
}

uint8_t TiBq27520Monitor::RegistersByte(uint32_t offset) const {
    return emu_.Get<TiBq27520Flash>().ReadDataFlash(kBq27520RegistersBase + offset);
}

int32_t TiBq27520Monitor::DischargeVoltage(uint32_t offset) const {
    const auto& flash = emu_.Get<TiBq27520Flash>();
    return static_cast<int16_t>((flash.ReadDataFlash(kBq27520DischargeBase + offset) << 8) |
                                flash.ReadDataFlash(kBq27520DischargeBase + offset + 1u));
}

bool TiBq27520Monitor::InsertionDetectEnabled() const {
    return (RegistersByte(kRegOpConfigB) & kOpConfigBBie) != 0u;
}

void TiBq27520Monitor::Baseline() {
    reported_soc_      = battery_->ChargePercent();
    reported_charging_ = battery_->Charging();
    sysdown_ = static_cast<int32_t>(battery_->VoltageMv()) < DischargeVoltage(kDischargeSysDownSet);
}

void TiBq27520Monitor::Restart() {
    clock_->Disarm(slot_event_);
    slot_pulse_armed_ = false;
    pulsed_slot_      = -1;
    slot_origin_ns_   = clock_->NowNs();
    bat_det_          = false;
    Baseline();
    if ((RegistersByte(kRegOpConfigLow) & kOpConfigIntFocv) != 0u) Pulse();
}

bool TiBq27520Monitor::BatteryDetected() const {
    return InsertionDetectEnabled() || bat_det_;
}

void TiBq27520Monitor::InsertBattery() {
    if (!InsertionDetectEnabled()) bat_det_ = true;
}

void TiBq27520Monitor::RemoveBattery() {
    if (!InsertionDetectEnabled()) bat_det_ = false;
}

void TiBq27520Monitor::RequireSoc1Disabled() const {
    const uint8_t opconfig_b = RegistersByte(kRegOpConfigB);
    if ((opconfig_b & kOpConfigBBlInt) != 0u) {
        emu_.Get<Fatal>().Die("ti bq27520: OpConfig B 0x%02X enables the SOC1 flag and interrupt, which are not "
                              "modeled", opconfig_b);
    }
}

bool TiBq27520Monitor::Soc1() const {
    RequireSoc1Disabled();
    return false;
}

bool TiBq27520Monitor::CrossesSocPoint(int from, int to) const {
    const int delta = RegistersByte(kRegSocDelta);
    if (delta == 0 || from == to) return false;
    if (to < from && to <= kSocEmpty) return true;
    if (to > from && to >= kSocFull) return true;
    for (int point = kSocFull; point >= kSocEmpty; point -= delta) {
        if (to < from ? (to <= point && from > point) : (to > point && from <= point)) return true;
    }
    return false;
}

void TiBq27520Monitor::OnBatteryChange() {
    if (rom_->Active()) return;
    RequireSoc1Disabled();
    const int     soc      = battery_->ChargePercent();
    const bool    charging = battery_->Charging();
    const int32_t mv       = static_cast<int32_t>(battery_->VoltageMv());
    bool pulse = CrossesSocPoint(std::exchange(reported_soc_, soc), soc);
    if (std::exchange(reported_charging_, charging) != charging && RegistersByte(kRegSocDelta) != 0u &&
        (RegistersByte(kRegOpConfigD) & kOpConfigDSocState) != 0u) {
        pulse = true;
    }
    if (!sysdown_ && mv < DischargeVoltage(kDischargeSysDownSet)) {
        sysdown_ = true;
        pulse    = true;
    } else if (sysdown_ && mv > DischargeVoltage(kDischargeSysDownClear)) {
        sysdown_ = false;
        pulse    = true;
    }
    if (pulse) RequestPulse();
}

void TiBq27520Monitor::IndicateDataFlashWrite() {
    const uint32_t wait = (static_cast<uint32_t>(RegistersByte(kRegDfWrIndWaitTimeHi)) << 8) |
                          RegistersByte(kRegDfWrIndWaitTimeLo);
    if (wait == 0u || (RegistersByte(kRegOpConfigD) & kOpConfigDSocDfw) == 0u ||
        (RegistersByte(kRegOpConfigB) & kOpConfigBDfWrIndBl) != 0u) {
        return;
    }
    const int64_t slot = SlotAt(clock_->NowNs());
    if (slot == pulsed_slot_) {
        emu_.Get<Fatal>().Die("ti bq27520: a data flash write indication falls in one-second slot %lld, which "
                              "already carried a SOC_INT pulse", static_cast<long long>(slot));
    }
    RequestPulse();
}

int64_t TiBq27520Monitor::SlotAt(int64_t now_ns) const {
    return (now_ns - slot_origin_ns_) / kSlotNs;
}

void TiBq27520Monitor::RequestPulse() {
    const int64_t slot = SlotAt(clock_->NowNs());
    if (slot != pulsed_slot_) {
        pulsed_slot_ = slot;
        Pulse();
        return;
    }
    if (slot_pulse_armed_) return;
    LOG(Periph, "ti bq27520: SOC_INT pulse deferred to one-second slot %lld\n",
        static_cast<long long>(pulsed_slot_ + 1));
    slot_pulse_armed_ = true;
    ArmSlotPulse();
}

void TiBq27520Monitor::ArmSlotPulse() {
    clock_->Arm(slot_event_, clock_->CycleAtNs(slot_origin_ns_ + (pulsed_slot_ + 1) * kSlotNs));
}

void TiBq27520Monitor::DeliverSlotPulse() {
    slot_pulse_armed_ = false;
    pulsed_slot_      = SlotAt(clock_->NowNs());
    Pulse();
}

void TiBq27520Monitor::Pulse() {
    const uint8_t opconfig_low = RegistersByte(kRegOpConfigLow);
    if ((opconfig_low & kOpConfigSociPol) != 0u) {
        emu_.Get<Fatal>().Die("ti bq27520: Operation Configuration low byte 0x%02X selects an active-high "
                              "SOC_INT, which is not modeled", opconfig_low);
    }
    LOG(Periph, "ti bq27520: SOC_INT pulse, soc %d, sysdown %u\n", reported_soc_, sysdown_ ? 1u : 0u);
    soc_int_.Call(false);
    soc_int_.Call(true);
}

void TiBq27520Monitor::SaveState(StateWriter& w) {
    w.Write<int32_t>("bq27520_reported_soc", reported_soc_);
    w.Write<uint8_t>("bq27520_reported_charging", reported_charging_ ? 1u : 0u);
    w.Write<uint8_t>("bq27520_sysdown", sysdown_ ? 1u : 0u);
    w.Write<uint8_t>("bq27520_bat_det", bat_det_ ? 1u : 0u);
    w.Write<int64_t>("bq27520_slot_elapsed_ns", clock_->NowNs() - slot_origin_ns_);
    w.Write<int64_t>("bq27520_pulsed_slot", pulsed_slot_);
    w.Write<uint8_t>("bq27520_slot_pulse_armed", slot_pulse_armed_ ? 1u : 0u);
}

void TiBq27520Monitor::RestoreState(StateReader& r) {
    uint8_t charging = 0u, sysdown = 0u, bat_det = 0u, armed = 0u;
    int64_t elapsed  = 0;
    r.Read("bq27520_reported_soc", reported_soc_);
    r.Read("bq27520_reported_charging", charging);
    r.Read("bq27520_sysdown", sysdown);
    r.Read("bq27520_bat_det", bat_det);
    r.Read("bq27520_slot_elapsed_ns", elapsed);
    r.Read("bq27520_pulsed_slot", pulsed_slot_);
    r.Read("bq27520_slot_pulse_armed", armed);
    reported_charging_ = charging != 0u;
    sysdown_           = sysdown != 0u;
    bat_det_           = bat_det != 0u;
    slot_origin_ns_    = clock_->NowNs() - elapsed;
    slot_pulse_armed_  = armed != 0u;
    if (slot_pulse_armed_) {
        ArmSlotPulse();
    } else {
        clock_->Disarm(slot_event_);
    }
}

REGISTER_SERVICE(TiBq27520Monitor);

}
