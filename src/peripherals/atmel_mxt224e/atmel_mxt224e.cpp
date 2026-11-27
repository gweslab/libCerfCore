#include "atmel_mxt224e.h"

#include "atmel_mxt224e_config_checks.h"
#include "atmel_mxt224e_nvm.h"
#include "atmel_mxt224e_touchscreen.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <span>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint16_t kChecksumModeBit = 0x8000u;

constexpr uint16_t kT6Reset     = 0u;
constexpr uint16_t kT6Backup    = 1u;
constexpr uint16_t kT6Calibrate = 2u;
constexpr uint16_t kT6ReportAll = 3u;
constexpr uint16_t kT6Reserved  = 4u;
constexpr uint16_t kT6Diagnostic = 5u;
constexpr uint8_t  kBackupValue     = 0x55u;
constexpr uint8_t  kBootloaderValue = 0xA5u;

constexpr uint8_t kStatusReset  = 0x80u;
constexpr uint8_t kStatusCfgErr = 0x08u;

constexpr uint8_t  kChgHigh       = 2u;
constexpr uint16_t kT9Offset      = kMxtT9Ctrl - kMxtNvmStart;
constexpr uint16_t kT9XOrigin     = kMxtT9XOrigin - kMxtNvmStart;

constexpr int64_t kConfigErrorRepeatNs = 200'000'000;

}

void AtmelMxt224e::OnReady() {
    BuildInfoBlock();
    auto& clock   = emu_.Get<GuestCycleClock>();
    cfgerr_event_ = clock.Add([this] {
        cfgerr_armed_ = false;
        if (!cfgerr_) return;
        if (emu_.Get<AtmelMxt224eTouchscreen>().Sleeping()) {
            status_pending_ = true;
            return;
        }
        PostStatus();
        ArmConfigErrorRepeat();
        DriveChg();
    });
    clock.RegisterRateListener([this] {
        auto& c = emu_.Get<GuestCycleClock>();
        if (cfgerr_armed_) c.Arm(cfgerr_event_, c.CycleAtNs(cfgerr_due_ns_));
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { DriveChg(); });
    PowerOn();
}

void AtmelMxt224e::BuildInfoBlock() {
    info_ = {kMxtFamilyId, kMxtVariantId, kMxtVersion, kMxtBuild,
             kMxtMatrixX,  kMxtMatrixY,   static_cast<uint8_t>(kMxtObjectCount)};
    uint16_t at = kMxtIdBytes;
    for (const AtmelMxt224eObject& o : kMxtObjects) {
        info_[at++] = o.type;
        cerf::le::Put16(&info_[at], o.start);
        at += 2u;
        info_[at++] = static_cast<uint8_t>(o.size - 1u);
        info_[at++] = 0u;
        info_[at++] = o.report_ids;
    }
    cerf::le::Put24(&info_[at], AtmelMxt224eNvm::Crc24(std::span<const uint8_t>(info_.data(), kMxtTableEnd)));
}

void AtmelMxt224e::PowerOn() {
    ram_ = emu_.Get<AtmelMxt224eNvm>().Bytes();
    auto& checks = emu_.Get<AtmelMxt224eConfigChecks>();
    checks.RequireModeled(ram_);
    active_mode_    = Config(kMxtT46Mode);
    status_pending_ = false;
    t6_.fill(0u);
    queued_         = 0u;
    current_.fill(0u);
    pointer_        = 0u;
    cursor_         = 0u;
    address_bytes_  = 0u;
    wrote_          = false;
    t6_written_     = false;
    head_offered_   = false;
    chg_held_high_  = false;
    status_one_off_ = kStatusReset;
    if (cfgerr_armed_) emu_.Get<GuestCycleClock>().Disarm(cfgerr_event_);
    cfgerr_armed_   = false;
    cfgerr_ = checks.ConfigError(ram_, active_mode_);
    emu_.Get<AtmelMxt224eTouchscreen>().ResetTouch();
    if (in_reset_) return;
    PostStatus();
    if (cfgerr_) ArmConfigErrorRepeat();
}

bool AtmelMxt224e::MatchesAddress(uint8_t slave_addr) const {
    return !in_reset_ && slave_addr == DeviceAddress();
}

bool AtmelMxt224e::OwnsAddress(uint8_t slave_addr) const { return slave_addr == DeviceAddress(); }

void AtmelMxt224e::SetResetLine(bool asserted) {
    if (asserted == in_reset_) return;
    in_reset_ = asserted;
    if (asserted) {
        if (cfgerr_armed_) emu_.Get<GuestCycleClock>().Disarm(cfgerr_event_);
        cfgerr_armed_ = false;
        queued_       = 0u;
        head_offered_ = false;
        emu_.Get<AtmelMxt224eTouchscreen>().ResetTouch();
        DriveChg();
        return;
    }
    PowerOn();
    DriveChg();
}

uint16_t AtmelMxt224e::ConfigRange(uint16_t addr) const {
    const uint16_t range = cerf::le::U16(ram_.data() + addr - kMxtNvmStart);
    return MxtEffectiveRange(range);
}

void AtmelMxt224e::BusStart() {
    wrote_ = false;
    std::copy_n(ram_.begin() + kT9Offset, t9_before_.size(), t9_before_.begin());
}

void AtmelMxt224e::TxnStart(uint8_t) {
    address_bytes_ = 0u;
    head_offered_  = false;
    cursor_        = pointer_;
}

bool AtmelMxt224e::TxnWriteByte(uint8_t, uint8_t byte) {
    if (address_bytes_ == 0u) {
        address_low_   = byte;
        address_bytes_ = 1u;
        return true;
    }
    if (address_bytes_ == 1u) {
        const uint16_t addr = static_cast<uint16_t>(address_low_ | (byte << 8u));
        if ((addr & kChecksumModeBit) != 0u) {
            emu_.Get<Fatal>().Die("atmel mxt224e: write of address 0x%04X in checksum mode", addr);
        }
        pointer_       = addr;
        cursor_        = addr;
        address_bytes_ = 2u;
        return true;
    }
    WriteByteAt(cursor_++, byte);
    return true;
}

uint8_t AtmelMxt224e::TxnReadByte(uint8_t) {
    if (address_bytes_ == 1u) emu_.Get<Fatal>().Die("atmel mxt224e: read after one address byte");
    const uint16_t at    = cursor_;
    const uint8_t  value = ReadByteAt(at);
    const bool wraps = pointer_ == kMxtT44Start || pointer_ == kMxtT5Start;
    if (wraps && at >= kMxtT5Start && at < kMxtT6Start && at - kMxtT5Start >= kMxtMessageBytes) {
        cursor_ = kMxtT5Start;
    } else {
        ++cursor_;
    }
    return value;
}

void AtmelMxt224e::BusStop() {
    FinishWrite();
    head_offered_  = false;
    chg_held_high_ = false;
    cursor_        = pointer_;
    address_bytes_ = 0u;
    DriveChg();
}

uint8_t AtmelMxt224e::ReadByteAt(uint16_t addr) {
    if (addr < kMxtInfoBlockEnd) return info_[addr];
    if (addr == kMxtT44Start) return queued_;
    if (addr >= kMxtT5Start && addr < kMxtT6Start) return ReadMessageByte(addr - kMxtT5Start);
    if (addr >= kMxtT6Start && addr < kMxtT38Start) return t6_[addr - kMxtT6Start];
    if (addr >= kMxtNvmStart && addr < kMxtMapEnd) return ram_[addr - kMxtNvmStart];
    emu_.Get<Fatal>().Die("atmel mxt224e: read of address 0x%04X", addr);
}

uint8_t AtmelMxt224e::ReadMessageByte(uint16_t offset) {
    if (offset == 0u) {
        if (queued_ == 0u) {
            current_.fill(0u);
            return kMxtReportIdInvalid;
        }
        head_offered_ = true;
        return order_[0];
    }
    if (offset > kMxtMessageBytes) {
        emu_.Get<Fatal>().Die("atmel mxt224e: read of the T5 checksum byte without checksum mode");
    }
    if (head_offered_) {
        head_offered_ = false;
        PopHead();
    }
    return current_[offset - 1u];
}

void AtmelMxt224e::WriteByteAt(uint16_t addr, uint8_t value) {
    if (addr >= kMxtT6Start && addr < kMxtT38Start) {
        t6_[addr - kMxtT6Start] = value;
        t6_written_             = true;
    } else if (addr >= kMxtNvmStart && addr < kMxtMapEnd) {
        ram_[addr - kMxtNvmStart] = value;
    } else {
        emu_.Get<Fatal>().Die("atmel mxt224e: write of 0x%02X to address 0x%04X", value, addr);
    }
    wrote_ = true;
}

void AtmelMxt224e::FinishWrite() {
    if (!wrote_) return;
    wrote_ = false;
    if (t6_written_ && RunCommands()) return;
    emu_.Get<AtmelMxt224eConfigChecks>().RequireModeled(ram_);
    const bool error_before = cfgerr_;
    UpdateConfigError();
    const bool enable_toggled = ((t9_before_[0] ^ Config(kMxtT9Ctrl)) & kMxtCtrlEnable) != 0u;
    const bool geometry_moved = !std::equal(t9_before_.begin() + 1, t9_before_.end(), ram_.begin() + kT9XOrigin);
    if (!cfgerr_ && !error_before && (enable_toggled || geometry_moved) && TouchObjectEnabled()) ReportStatus();
    auto& touchscreen = emu_.Get<AtmelMxt224eTouchscreen>();
    if (status_pending_ && !touchscreen.Sleeping()) {
        status_pending_ = false;
        PostStatus();
        if (cfgerr_ && !cfgerr_armed_) ArmConfigErrorRepeat();
    }
    touchscreen.ConfigChanged();
}

bool AtmelMxt224e::RunCommands() {
    const auto fields = t6_;
    t6_.fill(0u);
    t6_written_ = false;
    if (fields[kT6Reserved] != 0u || fields[kT6Diagnostic] != 0u) {
        emu_.Get<Fatal>().Die("atmel mxt224e: T6 command byte 4 0x%02X, diagnostic 0x%02X", fields[kT6Reserved],
                              fields[kT6Diagnostic]);
    }
    if (fields[kT6Reset] == kBootloaderValue) emu_.Get<Fatal>().Die("atmel mxt224e: reset into the bootloader");
    if (fields[kT6Reset] != 0u) {
        PowerOn();
        return true;
    }
    if (fields[kT6Backup] != 0u) {
        if (fields[kT6Backup] != kBackupValue) {
            emu_.Get<Fatal>().Die("atmel mxt224e: T6 BACKUPNV 0x%02X", fields[kT6Backup]);
        }
        emu_.Get<AtmelMxt224eNvm>().Backup(ram_);
        ReportStatus();
    }
    if (fields[kT6Calibrate] != 0u && TouchObjectEnabled() && !cfgerr_) ReportStatus();
    if (fields[kT6ReportAll] != 0u) {
        ReportStatus();
        emu_.Get<AtmelMxt224eTouchscreen>().ReportAll();
    }
    return false;
}

bool AtmelMxt224e::TouchObjectEnabled() const { return (Config(kMxtT9Ctrl) & kMxtCtrlEnable) != 0u; }

bool AtmelMxt224e::TouchscreenScanning() const { return !in_reset_ && TouchObjectEnabled() && !cfgerr_; }

void AtmelMxt224e::UpdateConfigError() {
    const bool error = emu_.Get<AtmelMxt224eConfigChecks>().ConfigError(ram_, active_mode_);
    if (error == cfgerr_) return;
    cfgerr_ = error;
    ReportStatus();
    if (cfgerr_) {
        ArmConfigErrorRepeat();
    } else if (cfgerr_armed_) {
        emu_.Get<GuestCycleClock>().Disarm(cfgerr_event_);
        cfgerr_armed_ = false;
    }
}

void AtmelMxt224e::ArmConfigErrorRepeat() {
    auto& clock    = emu_.Get<GuestCycleClock>();
    cfgerr_due_ns_ = clock.NowNs() + kConfigErrorRepeatNs;
    cfgerr_armed_  = true;
    clock.Arm(cfgerr_event_, clock.CycleAtNs(cfgerr_due_ns_));
}

void AtmelMxt224e::ReportStatus() {
    if (emu_.Get<AtmelMxt224eTouchscreen>().Sleeping()) {
        status_pending_ = true;
        return;
    }
    PostStatus();
}

void AtmelMxt224e::PostStatus() {
    Message msg{};
    msg[0] = static_cast<uint8_t>(status_one_off_ | (cfgerr_ ? kStatusCfgErr : 0u));
    cerf::le::Put24(&msg[1], emu_.Get<AtmelMxt224eNvm>().Checksum());
    Post(kMxtReportIdT6, msg, false);
}

void AtmelMxt224e::PostTouch(uint8_t touch, const Message& data) {
    Post(static_cast<uint8_t>(kMxtReportIdT9First + touch), data, true);
    DriveChg();
}

void AtmelMxt224e::Post(uint8_t report_id, const Message& data, bool merge_events) {
    Message&   slot    = pending_[report_id - 1u];
    const auto end     = order_.begin() + queued_;
    const bool pending = std::find(order_.begin(), end, report_id) != end;
    if (pending && merge_events) {
        const uint8_t events = static_cast<uint8_t>((slot[0] | data[0]) & static_cast<uint8_t>(~kMxtT9Detect));
        slot    = data;
        slot[0] = static_cast<uint8_t>(events | (data[0] & kMxtT9Detect));
    } else {
        slot = data;
        if (!pending) order_[queued_++] = report_id;
    }
}

void AtmelMxt224e::PopHead() {
    const uint8_t report_id = order_[0];
    current_                = pending_[report_id - 1u];
    std::copy(order_.begin() + 1, order_.begin() + queued_, order_.begin());
    --queued_;
    if (report_id == kMxtReportIdT6) status_one_off_ = 0u;
    if ((Config(kMxtT18Ctrl) & kMxtT18Mode) == 0u) chg_held_high_ = true;
    DriveChg();
}

void AtmelMxt224e::DriveChg() {
    if (in_reset_) {
        chg_listeners_.Call(true);
        return;
    }
    const uint8_t command = Config(kMxtT18Command);
    bool          high    = queued_ == 0u || chg_held_high_;
    if (command == kChgHigh) high = true;
    if (command == kMxtT18ChgLow) high = false;
    chg_listeners_.Call(high);
}

void AtmelMxt224e::AddChgListener(CallbackList<bool>::Callback listener) {
    chg_listeners_.Add(std::move(listener));
}

void AtmelMxt224e::SaveState(StateWriter& w) {
    w.WriteBytes("mxt_ram", ram_.data(), ram_.size());
    w.WriteBytes("mxt_t6", t6_.data(), t6_.size());
    w.WriteBytes("mxt_pending", pending_[0].data(), sizeof(pending_));
    w.WriteBytes("mxt_order", order_.data(), order_.size());
    w.WriteBytes("mxt_current", current_.data(), current_.size());
    w.WriteBytes("mxt_t9_before", t9_before_.data(), t9_before_.size());
    w.Write<uint16_t>("mxt_pointer", pointer_);
    w.Write<uint16_t>("mxt_cursor", cursor_);
    w.Write<uint8_t>("mxt_address_low", address_low_);
    w.Write<uint8_t>("mxt_address_bytes", address_bytes_);
    w.Write<uint8_t>("mxt_queued", queued_);
    w.Write<uint8_t>("mxt_status_one_off", status_one_off_);
    w.Write<uint8_t>("mxt_active_mode", active_mode_);
    w.Write<uint8_t>("mxt_wrote", wrote_ ? 1u : 0u);
    w.Write<uint8_t>("mxt_t6_written", t6_written_ ? 1u : 0u);
    w.Write<uint8_t>("mxt_head_offered", head_offered_ ? 1u : 0u);
    w.Write<uint8_t>("mxt_chg_held_high", chg_held_high_ ? 1u : 0u);
    w.Write<uint8_t>("mxt_cfgerr", cfgerr_ ? 1u : 0u);
    w.Write<uint8_t>("mxt_cfgerr_armed", cfgerr_armed_ ? 1u : 0u);
    w.Write<uint8_t>("mxt_in_reset", in_reset_ ? 1u : 0u);
    w.Write<uint8_t>("mxt_status_pending", status_pending_ ? 1u : 0u);
    w.Write<int64_t>("mxt_cfgerr_left_ns", cfgerr_due_ns_ - emu_.Get<GuestCycleClock>().NowNs());
    emu_.Get<AtmelMxt224eTouchscreen>().SaveState(w);
}

void AtmelMxt224e::RestoreState(StateReader& r) {
    uint8_t flag    = 0u;
    int64_t left_ns = 0;
    r.ReadBytes("mxt_ram", ram_.data(), ram_.size());
    r.ReadBytes("mxt_t6", t6_.data(), t6_.size());
    r.ReadBytes("mxt_pending", pending_[0].data(), sizeof(pending_));
    r.ReadBytes("mxt_order", order_.data(), order_.size());
    r.ReadBytes("mxt_current", current_.data(), current_.size());
    r.ReadBytes("mxt_t9_before", t9_before_.data(), t9_before_.size());
    r.Read("mxt_pointer", pointer_);
    r.Read("mxt_cursor", cursor_);
    r.Read("mxt_address_low", address_low_);
    r.Read("mxt_address_bytes", address_bytes_);
    r.Read("mxt_queued", queued_);
    r.Read("mxt_status_one_off", status_one_off_);
    r.Read("mxt_active_mode", active_mode_);
    r.Read("mxt_wrote", flag);
    wrote_ = flag != 0u;
    r.Read("mxt_t6_written", flag);
    t6_written_ = flag != 0u;
    r.Read("mxt_head_offered", flag);
    head_offered_ = flag != 0u;
    r.Read("mxt_chg_held_high", flag);
    chg_held_high_ = flag != 0u;
    r.Read("mxt_cfgerr", flag);
    cfgerr_ = flag != 0u;
    r.Read("mxt_cfgerr_armed", flag);
    cfgerr_armed_ = flag != 0u;
    r.Read("mxt_in_reset", flag);
    in_reset_ = flag != 0u;
    r.Read("mxt_status_pending", flag);
    status_pending_ = flag != 0u;
    r.Read("mxt_cfgerr_left_ns", left_ns);
    cfgerr_due_ns_ = emu_.Get<GuestCycleClock>().NowNs() + left_ns;
    emu_.Get<AtmelMxt224eTouchscreen>().RestoreState(r);
}

void AtmelMxt224e::PostRestore() {
    auto& clock = emu_.Get<GuestCycleClock>();
    if (cfgerr_armed_) clock.Arm(cfgerr_event_, clock.CycleAtNs(cfgerr_due_ns_));
    emu_.Get<AtmelMxt224eTouchscreen>().PostRestore();
    DriveChg();
}

}
