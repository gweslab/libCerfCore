#include "pm8058_keypad.h"

#include "pm8058_irq.h"

#include "../../core/board_parts.h"
#include "qualcomm_pm8058_part_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

/* Linux drivers/input/keyboard/pm8058-keypad.c pm8058_keypad_probe:
   val |= 1 << 7 is the "can't enable kp" write. */
constexpr uint8_t kCtrlEnable = 1u << 7;

/* Linux drivers/input/keyboard/pm8058-keypad.c kp_hw_init:
   (drv_bits << 2) | (sns_bits << 5); kp_sense_irq_handler reads val & 0x3 as
   the events counter, and its comment names that counter gray coded. */
constexpr uint8_t  kCtrlDriveShift = 2u;
constexpr uint8_t  kCtrlDriveMask  = 0x7u;
constexpr uint8_t  kCtrlSenseShift = 5u;
constexpr uint8_t  kCtrlSenseMask  = 0x3u;
constexpr uint8_t  kCtrlEventMask  = 0x3u;
constexpr uint32_t kMaxEvents      = 3u;

/* Linux drivers/input/keyboard/pmic8xxx-keypad.c KEYP_SCAN_READ_STATE BIT(0),
   the ReadState bit of the RevB0 synchronous read protocol documented above
   pmic8xxx_chk_sync_read. */
constexpr uint8_t kScanReadState = 1u << 0;

constexpr uint8_t kSenseLineCount[] = {5u, 6u, 7u, 8u};
constexpr uint8_t kDriveLineCount[] = {5u, 6u, 7u, 8u, 10u, 12u, 15u, 18u};

/* Linux drivers/mfd/pm8058-core.c KEYPAD_IRQ_OFFSET (9 * 8 + 2). */
constexpr uint32_t kSenseIrq = 74u;

/* Linux drivers/input/keyboard/pm8058-keypad.c kp_process_scan_data:
   down = !(new[drv] & (1 << sns)), so an unpressed matrix reads all ones. */
constexpr uint8_t kNoKeysPressed = 0xFFu;

uint8_t GrayEncode(uint32_t value) {
    return (uint8_t)(value ^ (value >> 1));
}

}  // namespace

bool Pm8058Keypad::ShouldRegister() {
    return emu_.Get<BoardParts>().Has(PartId::QualcommPm8058);
}

void Pm8058Keypad::OnReady() {
    Reset();
    emu_.Get<GuestCpuReset>().RegisterResetListener(
        [this](ResetLineKind) { Reset(); });
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] { ReleaseHeldKeys(); });
}

void Pm8058Keypad::ReleaseHeldKeys() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (pressed_ == KeyMatrixCells<kMaxDrive>{}) return;
    pressed_.ReleaseAll();
    CaptureScan();
    LOG(Periph, "[PM8058] keypad keys held in the restored image released\n");
}

void Pm8058Keypad::Reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    ctrl_ = 0;
    scan_ = 0;
    pressed_.ReleaseAll();
    for (uint32_t i = 0; i < kMaxDrive; ++i) {
        latched_new_[i] = kNoKeysPressed;
        latched_old_[i] = kNoKeysPressed;
    }
    events_    = 0;
    new_index_ = 0;
    old_index_ = 0;
}

uint32_t Pm8058Keypad::DriveLines() const {
    return kDriveLineCount[(ctrl_ >> kCtrlDriveShift) & kCtrlDriveMask];
}

uint32_t Pm8058Keypad::SenseLines() const {
    return kSenseLineCount[(ctrl_ >> kCtrlSenseShift) & kCtrlSenseMask];
}

uint8_t Pm8058Keypad::ReadDataPort(uint8_t (&set)[kMaxDrive], uint32_t& index, bool delivers_events) {
    if ((ctrl_ & kCtrlEnable) == 0u) return kNoKeysPressed;

    const uint8_t value = set[index];
    if (++index >= DriveLines()) {
        index = 0;
        if (delivers_events) events_ = 0;
    }
    return value;
}

uint8_t Pm8058Keypad::ReadReg(uint16_t reg) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (reg) {
    case kRegCtrl:
        return (uint8_t)((ctrl_ & (uint8_t)~kCtrlEventMask) |
                         GrayEncode(events_));
    case kRegScan:    return scan_;
    case kRegNewData: return ReadDataPort(latched_new_, new_index_, true);
    case kRegOldData: return ReadDataPort(latched_old_, old_index_, false);
    default:
        emu_.Get<Fatal>().Die(
            "pm8058 keypad: register 0x%03X is not one of the four the block "
            "has", reg);
    }
}

void Pm8058Keypad::WriteReg(uint16_t reg, uint8_t value) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (reg) {
    case kRegCtrl: {
        const bool was_enabled = (ctrl_ & kCtrlEnable) != 0u;
        ctrl_ = (uint8_t)(value & (uint8_t)~kCtrlEventMask);
        if (!was_enabled && (ctrl_ & kCtrlEnable) != 0u &&
            (new_index_ != 0u || old_index_ != 0u)) {
            emu_.Get<Fatal>().Die(
                "pm8058 keypad: control 0x%02X enables the block with a data "
                "burst in flight at new %u old %u, and nothing models what the "
                "enable edge does to the read pointer",
                value, new_index_, old_index_);
        }
        return;
    }
    case kRegScan:
        if ((value & kScanReadState) != 0u) {
            emu_.Get<Fatal>().Die(
                "pm8058 keypad: the 0x%02X written to scan enters the "
                "synchronous read state, and nothing models the hold it places "
                "on the scan data", value);
        }
        scan_ = value;
        return;
    default:
        emu_.Get<Fatal>().Die(
            "pm8058 keypad: nothing models a write of 0x%02X to register "
            "0x%03X", value, reg);
    }
}

void Pm8058Keypad::SetKeyPressed(uint32_t drive, uint32_t sense, bool pressed) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (drive >= DriveLines() || sense >= SenseLines()) {
        emu_.Get<Fatal>().Die(
            "pm8058 keypad: key at drive %u sense %u is outside the %ux%u "
            "matrix the block is programmed to scan",
            drive, sense, DriveLines(), SenseLines());
    }

    if (!pressed_.Set(drive * KeyMatrixCells<kMaxDrive>::kColumns + sense, pressed)) return;

    CaptureScan();
}

void Pm8058Keypad::CaptureScan() {
    if ((ctrl_ & kCtrlEnable) == 0u) return;

    for (uint32_t i = 0; i < kMaxDrive; ++i) {
        latched_old_[i] = latched_new_[i];
        latched_new_[i] = static_cast<uint8_t>(~pressed_.Line(i));
    }
    if (events_ < kMaxEvents) ++events_;
    PublishIrq();
}

/* Linux drivers/input/keyboard/pm8058-keypad.c kp_stuck_irq_handler: a stuck
   key raises no event because it "doesn't get considered as key state change",
   so the sense source is the state-change event itself. */
void Pm8058Keypad::PublishIrq() {
    auto& irq = emu_.Get<Pm8058Irq>();
    irq.SetSourceLevel(kSenseIrq, true);
    irq.SetSourceLevel(kSenseIrq, false);
}

void Pm8058Keypad::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write<uint8_t>("ctrl", ctrl_);
    w.Write<uint8_t>("scan", scan_);
    for (uint32_t i = 0; i < kMaxDrive; ++i) {
        w.Write<uint8_t>("state", static_cast<uint8_t>(~pressed_.Line(i)));
        w.Write<uint8_t>("latched_new", latched_new_[i]);
        w.Write<uint8_t>("latched_old", latched_old_[i]);
    }
    w.Write<uint32_t>("events", events_);
    w.Write<uint32_t>("new_index", new_index_);
    w.Write<uint32_t>("old_index", old_index_);
}

void Pm8058Keypad::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read("ctrl", ctrl_);
    r.Read("scan", scan_);
    for (uint32_t i = 0; i < kMaxDrive; ++i) {
        uint8_t state = kNoKeysPressed;
        r.Read("state", state);
        pressed_.SetLine(i, static_cast<uint8_t>(~state));
        r.Read("latched_new", latched_new_[i]);
        r.Read("latched_old", latched_old_[i]);
    }
    r.Read("events", events_);
    r.Read("new_index", new_index_);
    r.Read("old_index", old_index_);
    if (new_index_ >= kMaxDrive || old_index_ >= kMaxDrive ||
        events_ > kMaxEvents) {
        r.Reject(
            "pm8058 keypad: restored indices %u and %u with %u events are "
            "outside the %u drive lines and %u events the block has",
            new_index_, old_index_, events_, kMaxDrive, kMaxEvents);
    }
}

REGISTER_SERVICE(Pm8058Keypad);

}
