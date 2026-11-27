#include "ibm_workpad_z50_battery_gauge.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "ibm_workpad_z50_battery.h"
#include "ibm_workpad_z50_family_id.h"

#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint8_t  kCmdRemCapAlarm     = 0x01u;
constexpr uint8_t  kCmdRemTimeAlarm    = 0x02u;
constexpr uint8_t  kCmdTemperature     = 0x08u;
constexpr uint8_t  kCmdVoltage         = 0x09u;
constexpr uint8_t  kCmdCurrent         = 0x0Au;
constexpr uint8_t  kCmdRelStateOfCharge = 0x0Du;
constexpr uint8_t  kCmdRemainingCap    = 0x0Fu;
constexpr uint8_t  kCmdFullChargeCap   = 0x10u;
constexpr uint8_t  kCmdRunTimeToEmpty  = 0x11u;
constexpr uint8_t  kCmdBatteryStatus   = 0x16u;
constexpr uint8_t  kCmdDesignCap       = 0x18u;
constexpr uint8_t  kCmdFullChargeStage = 0x1Fu;
constexpr uint8_t  kCmdManufacturerData = 0x23u;
constexpr uint8_t  kCmdFlags           = 0x2Fu;
constexpr uint8_t  kCmdEdv1            = 0x3Eu;
constexpr uint8_t  kCmdEdvf            = 0x3Fu;
constexpr uint8_t  kCmdProgram         = 0x6Au;

constexpr uint16_t kProgramCommit      = 0x0080u;
constexpr uint16_t kUnlockRemCapAlarm  = 0x6900u;
constexpr uint16_t kUnlockRemTimeAlarm = 0x4B00u;
constexpr int64_t  kProgramBusyNs      = 500000000;

constexpr uint16_t kPowerUpTemperature = 2930u;
constexpr uint16_t kNotDischarging     = 0xFFFFu;
constexpr uint8_t  kManufacturerDataLen = 5u;

constexpr uint16_t kStatusOverChargedAlarm     = 0x8000u;
constexpr uint16_t kStatusTerminateChargeAlarm = 0x4000u;
constexpr uint16_t kStatusRemCapAlarm          = 0x0200u;
constexpr uint16_t kStatusRemTimeAlarm         = 0x0100u;
constexpr uint16_t kStatusInitialized          = 0x0080u;
constexpr uint16_t kStatusDischarging          = 0x0040u;
constexpr uint16_t kStatusFullyCharged         = 0x0020u;

constexpr uint16_t kFlags2Chm  = 0x2000u;
constexpr uint16_t kFlags1Imin = 0x0040u;
constexpr uint16_t kFlags1Vq   = 0x0020u;
constexpr uint16_t kFlags1Vdq  = 0x0008u;
constexpr uint16_t kFlags1Edv1 = 0x0002u;

uint16_t TwosComplement(uint16_t mv) { return static_cast<uint16_t>(0x10000u - mv); }

}

REGISTER_SERVICE(IbmWorkpadZ50BatteryGauge);

bool IbmWorkpadZ50BatteryGauge::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
}

void IbmWorkpadZ50BatteryGauge::OnReady() {
    clock_    = &emu_.Get<GuestCycleClock>();
    battery_  = &emu_.Get<IbmWorkpadZ50Battery>();
    fcc_      = battery_->DesignCapacityMah();
    vdq_fill_ = battery_->FillPercent();
    TrackValidDischarge();
    battery_->RegisterChangeListener([this] { TrackValidDischarge(); });
}

bool IbmWorkpadZ50BatteryGauge::MatchesAddress(uint8_t slave_addr) const {
    return slave_addr == kAddress && clock_->NowNs() >= busy_until_ns_;
}

void IbmWorkpadZ50BatteryGauge::TxnStart(uint8_t) {
    cmd_.Start();
    write_count_ = 0;
    read_len_    = 0;
    read_idx_    = 0;
}

uint8_t IbmWorkpadZ50BatteryGauge::Command() const { return static_cast<uint8_t>(cmd_.Register()); }

bool IbmWorkpadZ50BatteryGauge::TxnWriteByte(uint8_t, uint8_t byte) {
    if (cmd_.Select(byte)) return true;
    if (write_count_ == 0u) {
        write_lo_    = byte;
        write_count_ = 1u;
        return true;
    }
    if (write_count_ != 1u) {
        emu_.Get<Fatal>().Die("z50 battery gauge: write byte 0x%02X after a word to command 0x%02X",
                              byte, Command());
    }
    write_count_ = 2u;
    WriteWord(Command(), static_cast<uint16_t>(write_lo_ | (byte << 8)));
    return true;
}

uint8_t IbmWorkpadZ50BatteryGauge::TxnReadByte(uint8_t) {
    if (read_idx_ == 0u) LatchRead();
    if (read_idx_ >= read_len_) {
        emu_.Get<Fatal>().Die("z50 battery gauge: read byte %u of command 0x%02X past its %u bytes",
                              read_idx_, Command(), read_len_);
    }
    return read_[read_idx_++];
}

void IbmWorkpadZ50BatteryGauge::LatchRead() {
    if (Command() == kCmdManufacturerData) {
        read_.fill(0);
        read_[0]  = kManufacturerDataLen;
        read_len_ = static_cast<uint8_t>(kManufacturerDataLen + 1u);
        return;
    }
    const uint16_t w = Word(Command());
    read_[0]  = static_cast<uint8_t>(w);
    read_[1]  = static_cast<uint8_t>(w >> 8);
    read_len_ = 2u;
}

uint16_t IbmWorkpadZ50BatteryGauge::RemainingMah() const {
    return static_cast<uint16_t>(fcc_ * battery_->FillPercent() / Battery::kFullPercent);
}

uint16_t IbmWorkpadZ50BatteryGauge::Word(uint8_t cmd) const {
    switch (cmd) {
        case kCmdRemCapAlarm:      return rca_;
        case kCmdRemTimeAlarm:     return rta_;
        case kCmdTemperature:      return kPowerUpTemperature;
        case kCmdVoltage:          return battery_->VoltageMv();
        case kCmdCurrent:          return static_cast<uint16_t>(battery_->CurrentMa());
        case kCmdRelStateOfCharge: return static_cast<uint16_t>(battery_->FillPercent());
        case kCmdRemainingCap:     return RemainingMah();
        case kCmdFullChargeCap:    return fcc_;
        case kCmdRunTimeToEmpty: {
            const int16_t ma = battery_->CurrentMa();
            if (ma >= 0) return kNotDischarging;
            const uint32_t minutes = RemainingMah() * 60u / static_cast<uint32_t>(-ma);
            return static_cast<uint16_t>(std::min<uint32_t>(minutes, kNotDischarging - 1u));
        }
        case kCmdBatteryStatus:    return BatteryStatus();
        case kCmdDesignCap:        return battery_->DesignCapacityMah();
        case kCmdFlags:            return Flags();
        case kCmdEdv1:             return TwosComplement(battery_->Edv1Mv());
        case kCmdEdvf:             return TwosComplement(battery_->EdvfMv());
        case kCmdProgram:          return 0u;
        default:
            emu_.Get<Fatal>().Die("z50 battery gauge: read of command 0x%02X", cmd);
    }
}

uint16_t IbmWorkpadZ50BatteryGauge::BatteryStatus() const {
    uint16_t s = kStatusInitialized;
    if (!battery_->Charging()) s |= kStatusDischarging;
    if (battery_->FillPercent() >= Battery::kFullPercent) s |= kStatusFullyCharged;
    if (battery_->ChargeComplete()) s |= kStatusOverChargedAlarm | kStatusTerminateChargeAlarm;
    if (RemainingMah() < rca_) s |= kStatusRemCapAlarm;
    if (battery_->CurrentMa() < 0 && Word(kCmdRunTimeToEmpty) < rta_) s |= kStatusRemTimeAlarm;
    return s;
}

uint16_t IbmWorkpadZ50BatteryGauge::Flags() const {
    uint16_t f = kFlags2Chm;
    if (battery_->ChargeComplete()) f |= kFlags1Imin;
    if (battery_->Charging()) f |= kFlags1Vq;
    {
        std::lock_guard<std::mutex> lk(vdq_mtx_);
        if (vdq_) f |= kFlags1Vdq;
    }
    if (battery_->VoltageMv() < battery_->Edv1Mv()) f |= kFlags1Edv1;
    return f;
}

void IbmWorkpadZ50BatteryGauge::TrackValidDischarge() {
    std::lock_guard<std::mutex> lk(vdq_mtx_);
    const int fill = battery_->FillPercent();
    if (battery_->Charging()) {
        vdq_ = false;
    } else if (battery_->Discharging() && fill >= Battery::kFullPercent) {
        vdq_ = true;
    } else if (fill > vdq_fill_) {
        vdq_ = false;
    }
    vdq_fill_ = fill;
}

bool IbmWorkpadZ50BatteryGauge::Unlocked() const {
    return rca_ == kUnlockRemCapAlarm && rta_ == kUnlockRemTimeAlarm;
}

void IbmWorkpadZ50BatteryGauge::WriteWord(uint8_t cmd, uint16_t value) {
    switch (cmd) {
        case kCmdRemCapAlarm:  rca_ = value; return;
        case kCmdRemTimeAlarm: rta_ = value; return;
        case kCmdFullChargeStage:
            if (!Unlocked()) {
                emu_.Get<Fatal>().Die("z50 battery gauge: command 0x1F write 0x%04X while locked",
                                      value);
            }
            staged_fcc_ = value;
            fcc_staged_ = true;
            return;
        case kCmdProgram:
            if (value != kProgramCommit) {
                emu_.Get<Fatal>().Die("z50 battery gauge: command 0x6A write 0x%04X", value);
            }
            if (fcc_staged_) fcc_ = staged_fcc_;
            fcc_staged_    = false;
            busy_until_ns_ = clock_->NowNs() + kProgramBusyNs;
            return;
        default:
            emu_.Get<Fatal>().Die("z50 battery gauge: write 0x%04X to command 0x%02X", value, cmd);
    }
}

void IbmWorkpadZ50BatteryGauge::SaveState(StateWriter& w) {
    w.Write("gauge_rca", rca_);
    w.Write("gauge_rta", rta_);
    w.Write("gauge_fcc", fcc_);
    w.Write("gauge_staged_fcc", staged_fcc_);
    w.Write("gauge_fcc_staged", static_cast<uint8_t>(fcc_staged_));
    w.Write("gauge_busy_left_ns", std::max<int64_t>(busy_until_ns_ - clock_->NowNs(), 0));
    w.Write("gauge_cmd", Command());
    w.Write("gauge_cmd_pending", static_cast<uint8_t>(cmd_.Pending()));
    w.Write("gauge_write_lo", write_lo_);
    w.Write("gauge_write_count", write_count_);
    w.WriteBytes("gauge_read", read_.data(), read_.size());
    w.Write("gauge_read_len", read_len_);
    w.Write("gauge_read_idx", read_idx_);
    std::lock_guard<std::mutex> lk(vdq_mtx_);
    w.Write("gauge_vdq", static_cast<uint8_t>(vdq_));
    w.Write("gauge_vdq_fill", static_cast<int32_t>(vdq_fill_));
}

void IbmWorkpadZ50BatteryGauge::RestoreState(StateReader& r) {
    uint8_t v = 0;
    int64_t busy_left = 0;
    r.Read("gauge_rca", rca_);
    r.Read("gauge_rta", rta_);
    r.Read("gauge_fcc", fcc_);
    r.Read("gauge_staged_fcc", staged_fcc_);
    r.Read("gauge_fcc_staged", v);
    fcc_staged_ = v != 0u;
    r.Read("gauge_busy_left_ns", busy_left);
    busy_until_ns_ = clock_->NowNs() + busy_left;
    uint8_t cmd = 0;
    r.Read("gauge_cmd", cmd);
    r.Read("gauge_cmd_pending", v);
    cmd_.Restore(cmd, true, v != 0u);
    r.Read("gauge_write_lo", write_lo_);
    r.Read("gauge_write_count", write_count_);
    r.ReadBytes("gauge_read", read_.data(), read_.size());
    r.Read("gauge_read_len", read_len_);
    r.Read("gauge_read_idx", read_idx_);
    int32_t fill = 0;
    std::lock_guard<std::mutex> lk(vdq_mtx_);
    r.Read("gauge_vdq", v);
    vdq_ = v != 0u;
    r.Read("gauge_vdq_fill", fill);
    vdq_fill_ = fill;
}

}
