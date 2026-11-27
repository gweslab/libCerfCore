#include "../../peripherals/usb/usb_state.h"
#include "imx51_usboh3.h"
#include "imx51_usb_device_transfers.h"
#include "usb_device_host.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "imx51_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "imx51_usb_transceiver_clocks.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../chipidea_ulpi_phys.h"
#include "../irq_controller.h"
#include "../../peripherals/usb/ulpi_phy.h"

#include <array>
#include <cstdint>

namespace cerfcore {

namespace {

using Ci = ChipIdeaUsbCore;

constexpr uint32_t kBase = 0x73F80000u;

constexpr uint32_t kOffCaplen   = 0x00000100u;  /* CAPLENGTH(b0)+HCIVERSION(b16) */
/* CAPLENGTH 0x40 (operational regs at cap+0x40) | HCIVERSION 0x0100 (EHCI 1.00). */
constexpr uint32_t kCapReset = 0x01000040u;
/* EHCI 1.0 Spec Table 2-5 (p13) + Table 2-6 (p14, N_PORTS bits 3:0). */
constexpr uint32_t kOffHcsparams = kOffCaplen + 0x04u;
constexpr uint32_t kNumPorts     = 1u;
constexpr uint32_t kHcsparams    = Ci::kHcsparamsPi | Ci::kHcsparamsPpc | kNumPorts;
/* MCIMX51RM §60.4.2.4.4 HCCPARAMS "Default Value: 0006h"; Table 60-87 USBCMD "00080B00h if
   Asynchronous Schedule Park Capability is a one". */
constexpr uint32_t kOffHccparams = kOffCaplen + 0x08u;
constexpr uint32_t kHccparamsBorn = 0x00000006u;
constexpr uint32_t kUsbcmdBorn    = 0x00080B00u;

constexpr uint32_t kPortscDevAttached =
    Ci::kPortscCcs | Ci::kPortscPe | Ci::kPortscPp | Ci::kPortscHsp | Ci::kPortscPspdHs;

constexpr uint32_t kOffOtgMirror = 0x00000804u;
constexpr uint32_t kMirrorUlpiPhyClock = 1u << 7;
constexpr uint32_t kMirrorUtmiPhyClock = 1u << 8;
constexpr uint32_t kOffPhyCtrl0  = 0x00000808u;
constexpr uint32_t kOffUsbCtrl1  = 0x00000810u;
/* MCIMX51RM Figure 60-4 (PHY_CTRL_0 reset row). */
constexpr uint32_t kPhyCtrl0Born = 0x80001400u;

/* USBSTS device interrupt bits (MCIMX51RM Ch 60, p2896-2897). */
constexpr uint32_t kStsUri = 1u << 6;   /* USB reset received */
constexpr uint32_t kDevIntBits = Ci::kUsbstsUi | Ci::kUsbstsUei | Ci::kUsbstsPci | kStsUri;
constexpr uint32_t kFrameStatusBits =
    FreescaleUsbFrameIndex::kStsSri | FreescaleUsbFrameIndex::kStsFri;
/* MCIMX51RM Table 60-2 (pages 60-7 / 60-8): USBCMD / USBSTS / USBINTR at Host1 + 0x340, Host2
   + 0x540, Host3 + 0x740. Table 3-2 (pages 3-1 / 3-2): 18 USB OTG, 14 USB Host 1, 16 USB Host 2,
   17 USB Host 3. */
constexpr uint32_t kCoreIrq[] = {18u, 14u, 16u, 17u};

constexpr uint32_t kOtgCoreIndex = 0u;

constexpr uint32_t kTxFillTuningBorn = 0x00020000u;
constexpr uint32_t kOtgscBorn        = 0x00000020u;

constexpr ChipIdeaUsbCoreRegValue kOtgPowerOn[] = {{Ci::kTxFillTuning, kTxFillTuningBorn},
                                                   {Ci::kOtgsc, kOtgscBorn}};
constexpr ChipIdeaUsbCoreRegValue kHostPowerOn[] = {{Ci::kTxFillTuning, kTxFillTuningBorn}};

}  /* namespace */

REGISTER_SERVICE(Imx51Usboh3);

ChipIdeaUsbCoreKind Imx51Usboh3::CoreKind(uint32_t core) {
    if (core == kOtgCoreIndex) return {kUsbcmdBorn, Ci::kPortscPtsSerial, false, kOtgPowerOn};
    return {kUsbcmdBorn, Ci::kPortscPtsSerial, true, kHostPowerOn};
}

bool Imx51Usboh3::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx51;
}

void Imx51Usboh3::OnReady() {
    auto* board_phys = emu_.TryGet<ChipIdeaUlpiPhys>();
    for (uint32_t core = 0; core < kCores; ++core) {
        phys_[core] = board_phys ? board_phys->Phy(core) : nullptr;
        cores_[core].AttachPhy(phys_[core]);
        cores_[core].Attach();
    }
    emu_.Get<PeripheralDispatcher>().Register(this);
    transfers_   = &emu_.Get<Imx51UsbDeviceTransfers>();
    clock_       = &emu_.Get<GuestCycleClock>();
    frame_event_ = clock_->Add([this] { OnScheduleTimer(); });
    irq_event_   = clock_->Add([this] { OnFrameIrqEvent(); });
    frame_index_.Attach();
    transceiver_clocks_ = &emu_.Get<Imx51UsbTransceiverClocks>();
    transceiver_clocks_->RegisterChangeListener([this](FreescaleLowPowerMode mode) {
        std::lock_guard<std::mutex> lk(async_schedule_mtx_);
        RefreshTransceiverClocks(mode);
    });
    clock_->RegisterRateListener([this] { OnCpuRateChanged(); });
    /* MCIMX51RM Table 54-10: system_rst_b "Resets functional modules" in every reset row. */
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        std::lock_guard<std::mutex> lk(async_schedule_mtx_);
        if (kind == ResetLineKind::Rtc) {
            if (UsbDevice* dev = otg_host_root_port_.Device()) dev->ResetToDefault();
        }
        ResetController();
        RefreshDeviceIrq();
    });
    ResetController();
}

void Imx51Usboh3::ResetController() {
    regs_.fill(0u);
    for (uint32_t core = 0; core < kNonCore; core += kCoreSpan) {
        regs_[(core + kOffCaplen) >> 2]    = kCapReset;
        regs_[(core + kOffHcsparams) >> 2] = kHcsparams;
        regs_[(core + kOffHccparams) >> 2] = kHccparamsBorn;
        cores_[core / kCoreSpan].PowerOn();
    }
    regs_[kOffPhyCtrl0 >> 2]  = kPhyCtrl0Born;
    reset_seen_ = false;
    frame_index_.ResetAll();
    clock_->Disarm(irq_event_);
    frames_running_ = false;
    UpdateScheduleTimer();
    /* MCIMX51RM Table 60-52 CCS: "This value reflects the current state of the port". */
    if (otg_host_root_port_.IsConnected()) OnPortConnectChanged(0);
}

void Imx51Usboh3::ResetCore(uint32_t core) {
    frame_index_.Reset(core);
    cores_[core].ControllerReset();
    if (core != kOtgCoreIndex) return;
    reset_seen_ = false;
    if (otg_host_root_port_.IsConnected()) OnPortConnectChanged(0);
}

/* MCIMX51RM §60.4.5.4.1: micro-frame starts are "timed precisely to 125 us using the
   transceiver clock as a reference clock". */
void Imx51Usboh3::RefreshTransceiverClocks(FreescaleLowPowerMode mode) {
    bool changed = false;
    for (uint32_t core = 0; core < kCores; ++core) {
        if (!frame_index_.Running(core)) continue;
        const bool on = transceiver_clocks_->CoreClockRuns(
            core, regs_[(core * kCoreSpan + Ci::kPortsc) >> 2], regs_[kOffPhyCtrl0 >> 2],
            regs_[kOffUsbCtrl1 >> 2], mode);
        if (on == frame_index_.Clocked(core)) continue;
        frame_index_.SetClocked(core, on);
        changed = true;
    }
    if (!changed) return;
    UpdateScheduleTimer();
    ArmFrameIrq();
}

void Imx51Usboh3::UpdateScheduleTimer() {
    const auto cmd = regs_[Ci::kUsbcmd >> 2];
    if (Core0IsDevice() || !otg_host_root_port_.IsConnected() || !frame_index_.Clocked(0u) ||
        !(cmd & Ci::kUsbcmdRunStop) || !(cmd & (Ci::kUsbcmdAse | Ci::kUsbcmdPse))) {
        frames_running_ = false;
        clock_->Disarm(frame_event_);
        return;
    }
    if (frames_running_) return;
    frames_running_ = true;
    ArmNextFrame();
}

void Imx51Usboh3::ArmNextFrame() {
    clock_->Arm(frame_event_, frame_index_.NextFrameCycle(0u));
}

void Imx51Usboh3::OnScheduleTimer() {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    ExecuteAsyncSchedule();
    ExecutePeriodicSchedule();
    ArmNextFrame();
}

void Imx51Usboh3::OnCpuRateChanged() {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    frame_index_.Rescale();
    cores_[kOtgCoreIndex].Rescale();
    ArmFrameIrq();
    if (!frames_running_) return;
    const uint64_t now = clock_->Cycles();
    if (!clock_->IsDue(frame_event_, now)) ArmNextFrame();
}

uint32_t Imx51Usboh3::MmioBase() const { return kBase; }
uint32_t Imx51Usboh3::MmioSize() const { return kSize; }

uint8_t Imx51Usboh3::ReadByte(uint32_t addr) {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    const uint32_t off = addr - kBase;
    return static_cast<uint8_t>(ReadLocked(off & ~3u) >> ((off & 3u) * 8u));
}
uint16_t Imx51Usboh3::ReadHalf(uint32_t addr) {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    const uint32_t off = addr - kBase;
    return static_cast<uint16_t>(ReadLocked(off & ~3u) >> ((off & 2u) * 8u));
}
uint32_t Imx51Usboh3::ReadWord(uint32_t addr) {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    return ReadLocked(addr - kBase);
}

uint32_t Imx51Usboh3::ReadLocked(uint32_t off) {
    /* Device mode (core0): CERF is the always-present host, so PORTSC1 reflects a
       connected, enabled, high-speed device port (sub_8005D97C polls it). */
    if (off == Ci::kPortsc && Core0IsDevice())
        return cores_[kOtgCoreIndex].Portsc() | kPortscDevAttached;
    if (off == kOffOtgMirror) {
        uint32_t value = cores_[kOtgCoreIndex].OtgMirror();
        if (cores_[kOtgCoreIndex].UlpiClockRuns()) value |= kMirrorUlpiPhyClock;
        if (transceiver_clocks_->UtmiPhyClockOn(regs_[Ci::kPortsc >> 2], regs_[kOffPhyCtrl0 >> 2],
                                                FreescaleLowPowerMode::kRun)) {
            value |= kMirrorUtmiPhyClock;
        }
        return value;
    }
    if (off >= kNonCore) return regs_[off >> 2];
    const uint32_t coff = off % kCoreSpan;
    const uint32_t core = off / kCoreSpan;
    if (coff == Ci::kPortsc) return cores_[core].Portsc();
    if (off == Ci::kOtgsc) return cores_[kOtgCoreIndex].Otgsc();
    if (coff == Ci::kUsbsts) return cores_[core].Usbsts() | frame_index_.Status(core);
    if (coff == Ci::kFrindex) return frame_index_.Frindex(core);
    if (coff == Ci::kUlpiViewport) {
        uint32_t view = 0u;
        if (!cores_[core].ReadViewport(view))
            emu_.Get<Fatal>().Die("i.MX51 USB core %u: ULPI viewport read with no PHY", core);
        return view;
    }
    return regs_[off >> 2];
}

void Imx51Usboh3::WriteCoreFrameReg(uint32_t off, uint32_t value) {
    if ((off % kCoreSpan) != Ci::kFrindex) return;
    const uint32_t core = off / kCoreSpan;
    if (core == 0u && Core0IsDevice()) return;
    /* MCIMX51RM §60.4.2.6.4: "This register cannot be written unless the Host Controller is in
       the 'Halted' state as indicated by the HCHalted bit." */
    if (frame_index_.Running(core)) return;
    frame_index_.WriteFrindex(core, value);
    ArmFrameIrq();
}

void Imx51Usboh3::WriteWord(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    const uint32_t off = addr - kBase;
    if (off < kNonCore) {
        const uint32_t coff = off % kCoreSpan;
        const bool core0_dev = off < kCoreSpan && Core0IsDevice();
        if (off == Ci::kOtgsc) {
            cores_[kOtgCoreIndex].WriteOtgsc(value);
            RefreshDeviceIrq();
            ArmFrameIrq();
            return;
        }
        WriteCoreFrameReg(off, value);
        if (coff == Ci::kFrindex) return;
        if (coff == Ci::kUsbcmd) {
            const uint32_t core = off / kCoreSpan;
            const uint32_t old  = regs_[off >> 2];
            cores_[core].WriteUsbcmd(value);
            if (cores_[core].ResetPending()) ResetCore(core);
            value = regs_[off >> 2];
            frame_index_.SetFrameListSize(core, FreescaleUsbFrameIndex::FrameListSizeCode(value));
            ApplyFrameRole(core);
            RefreshTransceiverClocks(FreescaleLowPowerMode::kRun);
            if (core0_dev && (value & Ci::kUsbcmdRunStop) && !(old & Ci::kUsbcmdRunStop))
                regs_[Ci::kUsbsts >> 2] |= kStsUri | Ci::kUsbstsPci;
            if (off < kCoreSpan) UpdateScheduleTimer();
            RefreshDeviceIrq();
            ArmFrameIrq();
            return;
        }
        if (coff == Ci::kUsbsts) {
            const uint32_t old = regs_[off >> 2];
            cores_[off / kCoreSpan].WriteUsbsts(value);
            frame_index_.ClearStatus(off / kCoreSpan,
                                     value & kFrameStatusBits);
            RefreshDeviceIrq();
            if (core0_dev && (old & kStsUri) && (value & kStsUri)) reset_seen_ = true;
            ArmFrameIrq();
            return;
        }
        if (coff == Ci::kUsbintr) {
            regs_[off >> 2] = value;
            RefreshDeviceIrq();
            ArmFrameIrq();
            return;
        }
        if (core0_dev && (coff == Ci::kEndptSetupStat || coff == Ci::kEndptComplete)) {
            cores_[kOtgCoreIndex].ClearOnWrite(coff, value);
            return;
        }
        if (core0_dev && coff == Ci::kEndptPrime) {
            ExecutePrime(value);
            regs_[off >> 2] = 0;         /* prime self-clears once serviced */
            return;
        }
        if (core0_dev && coff == Ci::kEndptFlush) {
            regs_[off >> 2] = 0;
            /* The reset handler's ENDPTFLUSH is the first one after URI; by here
               it has already cleared ENDPTSETUPSTAT, so the host can deliver its
               first SETUP and it will survive to the next ISR. */
            if (reset_seen_) {
                reset_seen_ = false;
                if (UsbDeviceHost* host = transfers_->Host()) host->OnDeviceReset();
            }
            return;
        }
        if (coff == Ci::kUlpiViewport) {
            if (!cores_[off / kCoreSpan].WriteViewport(value))
                emu_.Get<Fatal>().Die("i.MX51 USB core %u: ULPI viewport word 0x%08X names an operation the PHY cannot take",
                                      off / kCoreSpan, value);
            RefreshTransceiverClocks(FreescaleLowPowerMode::kRun);
            RefreshDeviceIrq();
            return;
        }
        if (coff == Ci::kPortsc) {
            WritePortsc(off / kCoreSpan, value);
            RefreshTransceiverClocks(FreescaleLowPowerMode::kRun);
            return;
        }
        if (coff == Ci::kListAddr) {
            cores_[off / kCoreSpan].WriteListAddr(value);
            return;
        }
        if (off < kCoreSpan && ChipIdeaUsbCore::IsEndptCtrl(coff)) {
            cores_[kOtgCoreIndex].WriteEndptCtrl(ChipIdeaUsbCore::EndpointOf(coff), value);
            return;
        }
        if (coff == Ci::kUsbmode) {
            cores_[off / kCoreSpan].WriteUsbmode(value);
            if (off == Ci::kUsbmode) {
                ApplyFrameRole(0u);
                UpdateScheduleTimer();
                RefreshDeviceIrq();
                ArmFrameIrq();
            }
            return;
        }
    }
    if (off == kOffOtgMirror) {
        cores_[kOtgCoreIndex].WriteOtgMirror(value);
        RefreshDeviceIrq();
        ArmFrameIrq();
        return;
    }
    regs_[off >> 2] = value;
    if (off == kOffPhyCtrl0 || off == kOffUsbCtrl1) RefreshTransceiverClocks(FreescaleLowPowerMode::kRun);
}

void Imx51Usboh3::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    w.WriteBytes("regs", regs_.data(), sizeof(regs_));
    for (const UlpiPhy* phy : phys_) {
        if (phy != nullptr) phy->SaveState(w);
    }
    w.Write<uint8_t>("reset_seen", reset_seen_ ? 1 : 0);
    frame_index_.Save(w);
    cores_[kOtgCoreIndex].SaveState(w);
    if (UsbDeviceHost* host = transfers_->Host()) host->SaveState(w);
    otg_host_root_port_.SaveState(w);
}
void Imx51Usboh3::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    r.ReadBytes("regs", regs_.data(), sizeof(regs_));
    for (UlpiPhy* phy : phys_) {
        if (phy != nullptr) phy->RestoreState(r);
    }
    uint8_t b = 0; r.Read("reset_seen", b); reset_seen_ = b != 0;
    frame_index_.Restore(r);
    cores_[kOtgCoreIndex].RestoreState(r);
    if (UsbDeviceHost* host = transfers_->Host()) host->RestoreState(r);
    otg_host_root_port_.RestoreState(r);
    clock_->Disarm(frame_event_);
    clock_->Disarm(irq_event_);
}

void Imx51Usboh3::PostRestore() {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    otg_host_root_port_.PostRestore();
    RefreshTransceiverClocks(FreescaleLowPowerMode::kRun);
    RefreshDeviceIrq();
    frames_running_ = false;
    clock_->Disarm(frame_event_);
    UpdateScheduleTimer();
    ArmFrameIrq();
}

bool Imx51Usboh3::Core0IsDevice() const {
    return cores_[kOtgCoreIndex].DeviceMode();
}

/* MCIMX51RM Table 60-42: SRE and FRE raise the interrupt while SRI or FRI is set. */
void Imx51Usboh3::RefreshDeviceIrq() {
    static_assert(std::size(kCoreIrq) == kCores);
    auto& intc = emu_.Get<IrqController>();
    for (uint32_t core = 0; core < kCores; ++core) {
        const uint32_t base = core * kCoreSpan;
        const uint32_t sts  = regs_[(base + Ci::kUsbsts) >> 2] | frame_index_.Status(core);
        const bool otg      = core == kOtgCoreIndex && cores_[kOtgCoreIndex].OtgInterrupt();
        const bool pending  = otg || (sts & regs_[(base + Ci::kUsbintr) >> 2] &
                                      (kDevIntBits | kFrameStatusBits | Ci::kUsbstsUlpii)) != 0u;
        if (pending) intc.AssertIrq(static_cast<int>(kCoreIrq[core]));
        else         intc.DeAssertIrq(static_cast<int>(kCoreIrq[core]));
    }
}

void Imx51Usboh3::ApplyFrameRole(uint32_t core) {
    const bool rs     = (regs_[(core * kCoreSpan + Ci::kUsbcmd) >> 2] & Ci::kUsbcmdRunStop) != 0u;
    const bool device = core == 0u && Core0IsDevice();
    frame_index_.SetRunning(core, rs, device ? FreescaleUsbFrameIndex::Role::kDeviceHighSpeed
                                             : FreescaleUsbFrameIndex::Role::kHost);
}

void Imx51Usboh3::ArmFrameIrq() {
    uint64_t due = cores_[kOtgCoreIndex].NextOtgInterruptCycle();
    for (uint32_t core = 0; core < kCores; ++core) {
        const uint32_t enabled =
            regs_[(core * kCoreSpan + Ci::kUsbintr) >> 2] & kFrameStatusBits;
        if (enabled == 0u) continue;
        const uint64_t at = frame_index_.NextStatusCycle(core, enabled);
        if (at < due) due = at;
    }
    if (due == FreescaleUsbFrameIndex::kNever) clock_->Disarm(irq_event_);
    else                                   clock_->Arm(irq_event_, due);
}

void Imx51Usboh3::OnFrameIrqEvent() {
    std::lock_guard<std::mutex> lk(async_schedule_mtx_);
    RefreshDeviceIrq();
    ArmFrameIrq();
}

uint32_t Imx51Usboh3::DqhBase() const {
    return regs_[Ci::kListAddr >> 2];
}

void Imx51Usboh3::RegisterDeviceHost(UsbDeviceHost* host) {
    transfers_->SetHost(host);
}

void Imx51Usboh3::DeliverSetup(const uint8_t setup[8]) {
    if (!transfers_->WriteSetup(DqhBase(), setup)) return;
    regs_[Ci::kEndptSetupStat >> 2] |= 1u;
    regs_[Ci::kUsbsts >> 2] |= Ci::kUsbstsUi;
    RefreshDeviceIrq();
}

void Imx51Usboh3::ExecutePrime(uint32_t prime_bits) {
    const uint32_t done = transfers_->ExecutePrime(DqhBase(), prime_bits);
    if (done == 0u) return;
    regs_[Ci::kEndptComplete >> 2] |= done;
    regs_[Ci::kUsbsts >> 2] |= Ci::kUsbstsUi;
    RefreshDeviceIrq();
}

}
