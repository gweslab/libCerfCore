#include "chipidea_usb_core.h"

#include "../peripherals/usb/ulpi_phy.h"
#include "../peripherals/usb/usb_device.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint32_t kConfigFlagBorn = 1u;

constexpr uint32_t kUsbmodeIdle = 0x0u;

constexpr uint32_t kEndptCtrl0Enables = 0x00800080u;
constexpr uint32_t kEndptCtrl0Stalls  = 0x00010001u;
constexpr uint32_t kEndptCtrlStored   = 0x00AF00AFu;

constexpr uint32_t kListAddrDeviceReserved = 0x7FFu;
constexpr uint32_t kListAddrHostReserved   = 0x1Fu;

constexpr uint32_t kViewportWakeup    = 1u << 31;
constexpr uint32_t kViewportRun       = 1u << 30;
constexpr uint32_t kViewportWrite     = 1u << 29;
constexpr uint32_t kViewportSyncState = 1u << 27;
constexpr uint32_t kViewportPort      = 0x07000000u;
constexpr uint32_t kViewportAddrShift = 16u;
constexpr uint32_t kViewportReadData  = 0x0000FF00u;
constexpr uint32_t kViewportReadShift = 8u;
constexpr uint32_t kViewportPending   = kViewportWakeup | kViewportRun;

constexpr uint32_t kPortscCsc     = 1u << 1;
constexpr uint32_t kPortscPedc    = 1u << 3;
constexpr uint32_t kPortscOcc     = 1u << 5;
constexpr uint32_t kPortscFpr     = 1u << 6;
constexpr uint32_t kPortscSuspend = 1u << 7;
constexpr uint32_t kPortscPr      = 1u << 8;
constexpr uint32_t kPortscPic     = 0x3u << 14;
constexpr uint32_t kPortscPtc     = 0xFu << 16;
constexpr uint32_t kPortscWake    = 0x7u << 20;
constexpr uint32_t kPortscSts     = 1u << 29;

constexpr uint32_t kPortscUnmodeled = kPortscPic | kPortscPtc | kPortscSts;
constexpr uint32_t kPortscGuestFields =
    ChipIdeaUsbCore::kPortscPtsMask | ChipIdeaUsbCore::kPortscPhcd | ChipIdeaUsbCore::kPortscPfsc;
constexpr uint32_t kPortscLineStatus = ChipIdeaUsbCore::kPortscLineDp | ChipIdeaUsbCore::kPortscLineDm;

constexpr uint8_t kUlpiLineDp = 1u << 0;
constexpr uint8_t kUlpiLineDm = 1u << 1;

constexpr uint32_t kMirrorIdDig      = 1u << 0;
constexpr uint32_t kMirrorASessValid = 1u << 1;
constexpr uint32_t kMirrorBSessValid = 1u << 2;
constexpr uint32_t kMirrorVbusValid  = 1u << 3;
constexpr uint32_t kMirrorSessEnd    = 1u << 4;
static_assert((kMirrorIdDig | kMirrorASessValid | kMirrorBSessValid | kMirrorVbusValid | kMirrorSessEnd) ==
              ChipIdeaUsbCore::kOtgMirrorInputs);

constexpr uint64_t kMillisecondsPerSecond = 1000u;
constexpr uint32_t kPortscSpeed = ChipIdeaUsbCore::kPortscHsp | ChipIdeaUsbCore::kPortscPspdMask;
constexpr uint32_t kPortscUnpowered = ChipIdeaUsbCore::kPortscCcs | ChipIdeaUsbCore::kPortscPe | kPortscSuspend |
                                      kPortscPr | kPortscFpr | kPortscWake | kPortscSpeed;
constexpr uint32_t kPortscClearOnWrite = kPortscCsc | kPortscPedc | kPortscOcc;

}

ChipIdeaUsbCore::ChipIdeaUsbCore(CerfEmulator& emu, uint32_t* block, ChipIdeaUsbCoreKind kind, UlpiPhy* phy)
    : block_(block), kind_(kind), phy_(phy), millis_(emu, false) {}

void ChipIdeaUsbCore::Attach() {
    if (!kind_.host_only) millis_.Attach(kMillisecondsPerSecond, 1u);
}

void ChipIdeaUsbCore::AttachPhy(UlpiPhy* phy) { phy_ = phy; }

void ChipIdeaUsbCore::Rescale() { millis_.Rescale(); }

uint32_t ChipIdeaUsbCore::Reg(uint32_t off) const { return block_[off >> 2]; }

uint32_t& ChipIdeaUsbCore::At(uint32_t off) { return block_[off >> 2]; }

bool ChipIdeaUsbCore::IsEndptCtrl(uint32_t off) {
    return off >= kEndptCtrl0 && off < kEndptCtrl0 + 4u * kEndpoints && (off & 3u) == 0u;
}

uint32_t ChipIdeaUsbCore::EndpointOf(uint32_t off) { return (off - kEndptCtrl0) / 4u; }

bool ChipIdeaUsbCore::DeviceMode() const {
    return (Reg(kUsbmode) & kUsbmodeCm) == kUsbmodeDevice;
}

bool ChipIdeaUsbCore::HostMode() const {
    return (Reg(kUsbmode) & kUsbmodeCm) == kUsbmodeHost;
}

uint32_t ChipIdeaUsbCore::BornMode() const {
    return kind_.host_only ? kUsbmodeHost : kUsbmodeIdle;
}

void ChipIdeaUsbCore::PowerOn() {
    std::fill(block_ + (kUsbcmd >> 2), block_ + (kBlockBytes >> 2), 0u);
    At(kUsbcmd)     = kind_.usbcmd_born;
    At(kConfigFlag) = kConfigFlagBorn;
    At(kUsbmode)    = BornMode();
    At(kPortsc)     = kind_.portsc_born;
    if (!kind_.host_only) At(kEndptCtrl0) = kEndptCtrl0Enables;
    for (const ChipIdeaUsbCoreRegValue& reg : kind_.power_on) At(reg.off) = reg.value;
    if (kind_.host_only) return;
    mirror_            = 0u;
    otg_event_pending_ = false;
    SampleOtgInputs(false);
    millis_mark_ = millis_.Now();
}

void ChipIdeaUsbCore::ControllerReset() {
    At(kUsbcmd)        = kind_.usbcmd_born;
    At(kUsbsts)        = 0u;
    At(kUsbintr)       = 0u;
    At(kFrindex)       = 0u;
    At(kCtrlDsSegment) = 0u;
    At(kPeriodicList)  = 0u;
    At(kListAddr)      = 0u;
    At(kConfigFlag)    = kConfigFlagBorn;
    At(kUsbmode)       = (Reg(kUsbmode) & ~kUsbmodeCm) | BornMode();
    At(kPortsc)        = kind_.portsc_born;
}

void ChipIdeaUsbCore::WriteUsbcmd(uint32_t value) {
    At(kUsbcmd) = value | (Reg(kUsbcmd) & kUsbcmdReset);
}

bool ChipIdeaUsbCore::ResetPending() const { return (Reg(kUsbcmd) & kUsbcmdReset) != 0u; }

uint32_t ChipIdeaUsbCore::Usbsts() const {
    const uint32_t latched = Reg(kUsbsts) & ~kUsbstsReadOnly;
    if (DeviceMode()) return latched;
    const uint32_t cmd = Reg(kUsbcmd);
    uint32_t sts = latched | (Reg(kUsbsts) & kUsbstsReclaim);
    if ((cmd & kUsbcmdRunStop) == 0u) sts |= kUsbstsHalted;
    if ((cmd & kUsbcmdAse) != 0u) sts |= kUsbstsAsync;
    if ((cmd & kUsbcmdPse) != 0u) sts |= kUsbstsPeriodic;
    return sts;
}

void ChipIdeaUsbCore::WriteUsbsts(uint32_t value) { At(kUsbsts) &= ~(value & ~kUsbstsReadOnly); }

void ChipIdeaUsbCore::SetReclamation(bool on) {
    At(kUsbsts) = on ? (Reg(kUsbsts) | kUsbstsReclaim) : (Reg(kUsbsts) & ~kUsbstsReclaim);
}

bool ChipIdeaUsbCore::OneMsElapsed() { return millis_.Now() != millis_mark_; }

uint32_t ChipIdeaUsbCore::Otgsc() {
    uint32_t value = Reg(kOtgsc) & (kOtgscReadWrite | kOtgscInputs | kOtgscLatched);
    if ((millis_.Now() & 1u) != 0u) value |= kOtgscOneMsToggle;
    if (OneMsElapsed()) value |= kOtgscOneMsStatus;
    return value;
}

void ChipIdeaUsbCore::WriteOtgsc(uint32_t value) {
    if ((value & kOtgscOneMsStatus) != 0u) millis_mark_ = millis_.Now();
    const uint32_t kept = Reg(kOtgsc) & (kOtgscInputs | (kOtgscLatched & ~value));
    At(kOtgsc) = kept | (value & kOtgscReadWrite);
}

void ChipIdeaUsbCore::ReceivePhyEvent() {
    if (Synchronous()) {
        SampleOtgInputs(true);
        return;
    }
    otg_event_pending_ = true;
}

bool ChipIdeaUsbCore::UlpiClockRuns() const {
    return phy_ != nullptr && phy_->ClockRuns() && (Reg(kPortsc) & kPortscPhcd) == 0u;
}

void ChipIdeaUsbCore::ResumeOtgInputs() {
    if (!otg_event_pending_ || !Synchronous()) return;
    otg_event_pending_ = false;
    SampleOtgInputs(true);
}

void ChipIdeaUsbCore::WriteOtgMirror(uint32_t value) {
    mirror_ = value & kOtgMirrorInputs;
    if (phy_ == nullptr) SampleOtgInputs(true);
}

uint32_t ChipIdeaUsbCore::OtgInputs() const {
    uint32_t inputs = 0u;
    if (phy_ != nullptr) {
        const uint8_t utmi = phy_->UtmiOutputs();
        if ((utmi & UlpiPhy::kStatusIdGnd) == 0u) inputs |= kOtgscIdB;
        if ((utmi & UlpiPhy::kStatusVbusValid) != 0u) inputs |= kOtgscAvv;
        if ((utmi & UlpiPhy::kStatusSessValid) != 0u) inputs |= kOtgscAsv | kOtgscBsv;
        if ((utmi & UlpiPhy::kStatusSessEnd) != 0u) inputs |= kOtgscBse;
        return inputs;
    }
    if ((mirror_ & kMirrorIdDig) != 0u) inputs |= kOtgscIdB;
    if ((mirror_ & kMirrorVbusValid) != 0u) inputs |= kOtgscAvv;
    if ((mirror_ & kMirrorASessValid) != 0u) inputs |= kOtgscAsv;
    if ((mirror_ & kMirrorBSessValid) != 0u) inputs |= kOtgscBsv;
    if ((mirror_ & kMirrorSessEnd) != 0u) inputs |= kOtgscBse;
    return inputs;
}

void ChipIdeaUsbCore::SampleOtgInputs(bool latch) {
    const uint32_t inputs = OtgInputs();
    const uint32_t old    = Reg(kOtgsc) & kOtgscInputs;
    uint32_t       events = 0u;
    if (latch) {
        const uint32_t changed = old ^ inputs;
        if ((changed & kOtgscIdB) != 0u) events |= kOtgscIdis;
        if ((changed & kOtgscAvv) != 0u) events |= kOtgscAvvis;
        if ((changed & kOtgscAsv) != 0u) events |= kOtgscAsvis;
        if ((changed & kOtgscBsv) != 0u) events |= kOtgscBsvis;
        if ((inputs & ~old & kOtgscBse) != 0u) events |= kOtgscBseis;
    }
    At(kOtgsc) = (Reg(kOtgsc) & ~kOtgscInputs) | inputs | events;
}

bool ChipIdeaUsbCore::OtgInterrupt() {
    const uint32_t value = Otgsc();
    return ((value >> kOtgscStatusShift) & (value >> kOtgscEnableShift) & kOtgscEventMask) != 0u;
}

uint64_t ChipIdeaUsbCore::NextOtgInterruptCycle() {
    if ((Reg(kOtgsc) & kOtgscOneMsEnable) == 0u || OneMsElapsed()) return kNever;
    return millis_.CycleOf(millis_mark_ + 1u);
}

uint32_t ChipIdeaUsbCore::Portsc() const {
    uint32_t value = Reg(kPortsc) & ~kPortscLineStatus;
    if (phy_ == nullptr) return value;
    const uint8_t line = phy_->LineState();
    if ((line & kUlpiLineDp) != 0u) value |= kPortscLineDp;
    if ((line & kUlpiLineDm) != 0u) value |= kPortscLineDm;
    return value;
}

void ChipIdeaUsbCore::SaveState(StateWriter& w) {
    w.Write<uint32_t>("usb_otg_mirror", mirror_);
    w.Write<uint8_t>("usb_otg_event_pending", otg_event_pending_ ? 1u : 0u);
    millis_.Save(w, "usb_otg_ms_");
    w.Write<uint64_t>("usb_otg_ms_mark", millis_mark_);
}

void ChipIdeaUsbCore::RestoreState(StateReader& r) {
    r.Read("usb_otg_mirror", mirror_);
    uint8_t pending = 0u;
    r.Read("usb_otg_event_pending", pending);
    otg_event_pending_ = pending != 0u;
    millis_.Restore(r, "usb_otg_ms_");
    r.Read("usb_otg_ms_mark", millis_mark_);
}

void ChipIdeaUsbCore::WriteUsbmode(uint32_t value) {
    const uint32_t cm = Reg(kUsbmode) & kUsbmodeCm;
    const uint32_t kept_cm = cm == kUsbmodeIdle ? value & kUsbmodeCm : cm;
    At(kUsbmode) = (value & ~kUsbmodeCm) | kept_cm;
}

void ChipIdeaUsbCore::WriteListAddr(uint32_t value) {
    const uint32_t reserved = DeviceMode() ? kListAddrDeviceReserved : kListAddrHostReserved;
    At(kListAddr) = value & ~reserved;
}

void ChipIdeaUsbCore::WriteEndptCtrl(uint32_t endpoint, uint32_t value) {
    if (endpoint == 0u) {
        At(kEndptCtrl0) = kEndptCtrl0Enables | (value & kEndptCtrl0Stalls);
        return;
    }
    At(kEndptCtrl0 + 4u * endpoint) = value & kEndptCtrlStored;
}

void ChipIdeaUsbCore::ClearOnWrite(uint32_t off, uint32_t value) { At(off) &= ~value; }

ChipIdeaPortscWrite ChipIdeaUsbCore::WritePortsc(uint32_t value, const UsbDevice* attached) {
    const bool device = DeviceMode();
    const uint32_t unmodeled = kPortscUnmodeled | (device ? kPortscFpr : 0u);
    if ((value & unmodeled) != 0u) return ChipIdeaPortscWrite::kRejected;
    const uint32_t old = Reg(kPortsc);
    const bool suspend_request = (value & kPortscSuspend) != 0u && (old & kPortscSuspend) == 0u;
    const bool stays_enabled   = (old & kPortscPe) != 0u && (value & kPortscPe) != 0u;
    if (!device && suspend_request && !stays_enabled) return ChipIdeaPortscWrite::kRejected;
    uint32_t next = (old & ~kPortscGuestFields) | (value & kPortscGuestFields);
    if (device) {
        At(kPortsc) = next & ~kPortscWake;
        ResumeOtgInputs();
        return ChipIdeaPortscWrite::kApplied;
    }
    next = (next & ~(kPortscPp | kPortscWake)) | (value & (kPortscPp | kPortscWake));
    if ((next & kPortscPp) == 0u) {
        At(kPortsc) = next & ~kPortscUnpowered;
        ResumeOtgInputs();
        return ChipIdeaPortscWrite::kApplied;
    }
    if ((old & kPortscPp) == 0u && attached != nullptr) {
        next |= kPortscCcs | kPortscCsc;
        At(kUsbsts) |= kUsbstsPci;
    }
    if ((value & kPortscPe) == 0u) next &= ~kPortscPe;
    ChipIdeaPortscWrite result = ChipIdeaPortscWrite::kApplied;
    if ((value & kPortscPr) != 0u && (next & kPortscCcs) != 0u) {
        next = (next & ~kPortscSpeed) | kPortscPe | kPortscPedc;
        if (attached != nullptr && attached->HighSpeed() && (next & kPortscPfsc) == 0u) {
            next |= kPortscHsp | kPortscPspdHs;
        }
        At(kUsbsts) |= kUsbstsPci;
        result = ChipIdeaPortscWrite::kDeviceReset;
    }
    next &= ~(value & kPortscClearOnWrite);
    if ((value & kPortscSuspend) != 0u) next |= kPortscSuspend;
    if ((value & kPortscFpr) != 0u) next &= ~kPortscSuspend;
    At(kPortsc) = next;
    ResumeOtgInputs();
    return result;
}

bool ChipIdeaUsbCore::ReportPortConnect(bool attached) {
    if ((Reg(kPortsc) & kPortscPp) == 0u) return false;
    const uint32_t kept = attached ? Reg(kPortsc) | kPortscCcs
                                   : Reg(kPortsc) & ~(kPortscCcs | kPortscPe | kPortscSuspend | kPortscSpeed);
    At(kPortsc) = kept | kPortscCsc;
    At(kUsbsts) |= kUsbstsPci;
    return true;
}

bool ChipIdeaUsbCore::Synchronous() const {
    return phy_->Synchronous() && (Reg(kPortsc) & kPortscPhcd) == 0u;
}

bool ChipIdeaUsbCore::StoreViewport(uint32_t value) {
    if (phy_ == nullptr || (Reg(kUlpiViewport) & kViewportPending) != 0u ||
        (value & kViewportPending) == kViewportPending || (value & kViewportPort) != 0u) {
        return false;
    }
    At(kUlpiViewport) = value;
    return true;
}

bool ChipIdeaUsbCore::RunViewport() {
    const uint32_t view = Reg(kUlpiViewport);
    if ((view & kViewportWakeup) != 0u) {
        phy_->Wakeup();
        At(kPortsc) &= ~kPortscPhcd;
        At(kUlpiViewport) = view & ~kViewportWakeup;
        At(kUsbsts) |= kUsbstsUlpii;
        ResumeOtgInputs();
        return true;
    }
    if ((view & kViewportRun) == 0u) return true;
    if (!Synchronous()) return false;
    const uint8_t addr = static_cast<uint8_t>(view >> kViewportAddrShift);
    if ((view & kViewportWrite) != 0u) {
        if (!phy_->Write(addr, static_cast<uint8_t>(view))) return false;
        At(kUlpiViewport) = view & ~kViewportRun;
        At(kUsbsts) |= kUsbstsUlpii;
        return true;
    }
    uint8_t data = 0;
    if (!phy_->Read(addr, data)) return false;
    At(kUlpiViewport) = (view & ~kViewportRun & ~kViewportReadData) |
                        (static_cast<uint32_t>(data) << kViewportReadShift);
    At(kUsbsts) |= kUsbstsUlpii;
    return true;
}

bool ChipIdeaUsbCore::WriteViewport(uint32_t value) {
    return StoreViewport(value) && RunViewport();
}

bool ChipIdeaUsbCore::ReadViewport(uint32_t& value) const {
    if (phy_ == nullptr) return false;
    value = Reg(kUlpiViewport) & ~kViewportSyncState;
    if (Synchronous()) value |= kViewportSyncState;
    return true;
}

}
