#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../execution/guest_cycle_clock.h"
#include "imx31_avic.h"
#include "imx31_ccm.h"
#include "imx31_id.h"
#include "imx31_plls.h"
#include <cerfcore/core/fatal.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../peripherals/usb/ulpi_phy.h"
#include <cerfcore/state/state_stream.h>
#include "../chipidea_ulpi_phys.h"
#include "../chipidea_usb_core.h"
#include "../freescale_module_clocks.h"
#include "../freescale_timer_clocks.h"
#include "../freescale_usb_frame_index.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace cerfcore {

namespace {

using Ci = ChipIdeaUsbCore;

constexpr uint32_t kBase        = 0x43F88000u;
constexpr uint32_t kSize        = 0x00004000u;
constexpr uint32_t kOtgUsbcmdBorn = 0x00080000u;
constexpr uint32_t kUsbCtrl     = 0x600u;
constexpr uint32_t kOtgMirror   = 0x604u;
constexpr uint32_t kFrameStatusBits =
    FreescaleUsbFrameIndex::kStsSri | FreescaleUsbFrameIndex::kStsFri;

/* EHCI capability registers (read-only) - MCIMX31RM §32.9.4 Fig 32-18..32-21.
   0x100 word packs CAPLENGTH(0x40)|HCIVERSION(0x0100<<16); HCSPARAMS N_PORTS=1
   (a 0 there is "undefined" so the host driver needs ≥1); HCCPARAMS=0x0006. */
constexpr uint32_t kCapLength   = 0x100u;
constexpr uint32_t kHciVersion  = 0x102u;
constexpr uint32_t kHcsParams   = 0x104u;
constexpr uint32_t kHccParams   = 0x108u;
constexpr uint32_t kCapWord     = 0x01000040u;
constexpr uint32_t kNumPorts     = 1u;
constexpr uint32_t kHcsParamsVal = Ci::kHcsparamsPi | Ci::kHcsparamsPpc | kNumPorts;
constexpr uint32_t kHccParamsVal = 0x00000006u;
/* MCIMX31RM Table 2-3: interrupt 37, source "USB", "OTG". */
constexpr uint32_t kAvicSourceUsbOtg = 37u;

constexpr ChipIdeaUsbCoreKind kOtgCore{kOtgUsbcmdBorn, Ci::kPortscPtsSerial, false, {}};
constexpr uint32_t            kOtgCoreIndex = 0u;

class Imx31Usbotg : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }
    void OnReady() override {
        module_clocks_ = &emu_.Get<FreescaleModuleClocks>();
        timer_clocks_  = &emu_.Get<FreescaleTimerClocks>();
        plls_          = &emu_.Get<Imx31Plls>();
        clock_         = &emu_.Get<GuestCycleClock>();
        avic_          = &emu_.Get<Imx31Avic>();
        irq_event_     = clock_->Add([this] { ArmFrameIrq(); });
        frame_index_.Attach();
        frame_index_.SetClocked(0u, true);
        if (auto* board_phys = emu_.TryGet<ChipIdeaUlpiPhys>()) phy_ = board_phys->Phy(kOtgCoreIndex);
        core_.AttachPhy(phy_);
        core_.Attach();
        core_.PowerOn();
        clock_->RegisterRateListener([this] {
            frame_index_.Rescale();
            core_.Rescale();
            ArmFrameIrq();
        });
        module_clocks_->RegisterGateListener([this] { OnUsbClockChange(); });
        emu_.Get<Imx31Ccm>().RegisterRateListener([this] { OnUsbClockChange(); });
        clock_->RegisterIdleListener([this] { ApplyFrameClock(timer_clocks_->WfiMode()); });
        clock_->RegisterIdleExitListener([this] { ApplyFrameClock(FreescaleLowPowerMode::kRun); });
        /* MCIMX31RM §3.6.1: "periph_reset_out signal is connected to all peripherals except
           EMI". */
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            ResetController();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        if (addr - kBase == kCapLength) return 0x40u;     /* CAPLENGTH (Fig 32-18) */
        HaltUnsupportedAccess("ReadByte", addr, 0);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - kBase == kHciVersion) return 0x0100u;  /* HCIVERSION (Fig 32-19) */
        HaltUnsupportedAccess("ReadHalf", addr, 0);
    }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if (off == Ci::kPortsc) return core_.Portsc();
        if (Ci::IsEndptCtrl(off)) return core_.Reg(off);
        switch (off) {
            case kCapLength: return kCapWord;
            case kHcsParams: return kHcsParamsVal;
            case kHccParams: return kHccParamsVal;
            case Ci::kUsbsts: return core_.Usbsts() | frame_index_.Status(0u);
            case kUsbCtrl:   return usb_ctrl_;
            case kOtgMirror: return core_.OtgMirror();
            case Ci::kOtgsc: return core_.Otgsc();
            case Ci::kUlpiViewport: {
                uint32_t view = 0u;
                if (!core_.ReadViewport(view)) HaltUnsupportedAccess("ReadWord", addr, 0);
                return view;
            }
            case Ci::kFrindex: return frame_index_.Frindex(0u);
            case Ci::kUsbcmd:
            case Ci::kUsbintr:
            case Ci::kUsbmode:
            case Ci::kConfigFlag:
            case Ci::kCtrlDsSegment:
            case Ci::kPeriodicList:
            case Ci::kListAddr:
            case Ci::kTxFillTuning:
            case Ci::kEndptSetupStat:
            case Ci::kEndptPrime:
            case Ci::kEndptFlush:
            case Ci::kEndptStat:
            case Ci::kEndptComplete:
                return core_.Reg(off);
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        if (off == Ci::kPortsc) {
            if (core_.WritePortsc(value, nullptr) == ChipIdeaPortscWrite::kRejected) {
                HaltUnsupportedAccess("WriteWord", addr, value);
            }
            ApplyFrameClock(FreescaleLowPowerMode::kRun);
            return;
        }
        if (Ci::IsEndptCtrl(off)) {
            core_.WriteEndptCtrl(Ci::EndpointOf(off), value);
            return;
        }
        switch (off) {
            case Ci::kUsbcmd:
                core_.WriteUsbcmd(value);
                RunHeldStarts();
                frame_index_.SetFrameListSize(0u, FreescaleUsbFrameIndex::FrameListSizeCode(core_.Reg(Ci::kUsbcmd)));
                SetFrameRunning();
                return;
            case Ci::kUsbsts:
                core_.WriteUsbsts(value);
                frame_index_.ClearStatus(0u, value & kFrameStatusBits);
                ArmFrameIrq();
                return;
            case Ci::kUsbintr:
                Reg(off) = value;
                ArmFrameIrq();
                return;
            case kUsbCtrl:   usb_ctrl_   = value; return;
            case kOtgMirror:
                core_.WriteOtgMirror(value);
                ArmFrameIrq();
                return;
            case Ci::kUsbmode:
                core_.WriteUsbmode(value);
                SetFrameRunning();
                return;
            case Ci::kConfigFlag: Reg(off) = value & 1u; return;
            case Ci::kOtgsc:
                core_.WriteOtgsc(value);
                ArmFrameIrq();
                return;
            case Ci::kUlpiViewport:
                if (!core_.StoreViewport(value)) HaltUnsupportedAccess("WriteWord", addr, value);
                RunHeldStarts();
                ApplyFrameClock(FreescaleLowPowerMode::kRun);
                return;
            /* MCIMX31RM §32.9.5.4: "In device mode this register is read only"; "This register
               cannot be written unless the Host Controller is in the 'Halted' state". */
            case Ci::kFrindex:
                if (core_.DeviceMode() || (core_.Reg(Ci::kUsbcmd) & Ci::kUsbcmdRunStop) != 0u) {
                    return;
                }
                frame_index_.WriteFrindex(0u, value);
                ArmFrameIrq();
                return;
            case Ci::kCtrlDsSegment:
            case Ci::kPeriodicList:
            case Ci::kTxFillTuning:
                Reg(off) = value;
                return;
            case Ci::kListAddr:
                core_.WriteListAddr(value);
                return;
            case Ci::kEndptPrime:
                Reg(off) |= value;
                RunHeldStarts();
                return;
            case Ci::kEndptFlush:
                Reg(off) |= value;
                Reg(Ci::kEndptPrime) &= ~value;
                RunHeldStarts();
                return;
            case Ci::kEndptSetupStat:
            case Ci::kEndptComplete:
                core_.ClearOnWrite(off, value);
                return;
            case Ci::kEndptStat: return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes("usbotg_core_regs", core_regs_.data(), sizeof(core_regs_));
        w.Write("usb_ctrl", usb_ctrl_);
        frame_index_.Save(w);
        if (phy_ != nullptr) phy_->SaveState(w);
        core_.SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes("usbotg_core_regs", core_regs_.data(), sizeof(core_regs_));
        r.Read("usb_ctrl", usb_ctrl_);
        frame_index_.Restore(r);
        if (phy_ != nullptr) phy_->RestoreState(r);
        core_.RestoreState(r);
        clock_->Disarm(irq_event_);
    }
    void PostRestore() override { ApplyFrameClock(FreescaleLowPowerMode::kRun); }

private:
    uint32_t& Reg(uint32_t off) { return core_regs_[off >> 2]; }

    /* MCIMX31RM §3.4.4.3.1: usb_main_clk is the USB DPLL output; §3.4.4.3.2: "ipg_clk_usb_baud is
       the clock for the USBOTG module". */
    bool UpllRuns() const { return plls_->Running(1u); }
    bool CoreClocked() const { return module_clocks_->Running(FreescaleModule::kUsbotg) && UpllRuns(); }

    void OnUsbClockChange() {
        RunHeldStarts();
        SetFrameRunning();
    }

    void RunHeldStarts() {
        if (!CoreClocked()) return;
        if (core_.ResetPending()) {
            frame_index_.Reset(0u);
            core_.ControllerReset();
        }
        if (!core_.RunViewport()) {
            emu_.Get<Fatal>().Die("i.MX31 USBOTG: ULPI viewport word 0x%08X names an operation the PHY cannot take",
                                  core_.Reg(Ci::kUlpiViewport));
        }
        Reg(Ci::kEndptStat)  = (Reg(Ci::kEndptStat) & ~Reg(Ci::kEndptFlush)) | Reg(Ci::kEndptPrime);
        Reg(Ci::kEndptPrime) = 0u;
        Reg(Ci::kEndptFlush) = 0u;
    }

    void ResetController() {
        core_.PowerOn();
        usb_ctrl_ = 0u;
        frame_index_.ResetAll();
        clock_->Disarm(irq_event_);
        avic_->DeassertSource(kAvicSourceUsbOtg);
    }

    /* MCIMX31RM §32.9.5.4: the host controller's FRINDEX "updates every 125 microseconds";
       Table 32-24 SRI: a device "initialized to FS before connect" sets it "at an interval of
       1ms during the prelude to connect and chirp"; Table 32-23 RS: a device "attach event". */
    void SetFrameRunning() {
        const bool host = core_.HostMode();
        const bool rs   = (core_.Reg(Ci::kUsbcmd) & Ci::kUsbcmdRunStop) != 0u;
        frame_index_.SetRunning(0u, rs && (host || core_.DeviceMode()),
                                host ? FreescaleUsbFrameIndex::Role::kHost
                                     : FreescaleUsbFrameIndex::Role::kDevicePrelude);
        ApplyFrameClock(FreescaleLowPowerMode::kRun);
    }

    void ApplyFrameClock(FreescaleLowPowerMode mode) {
        frame_index_.SetClocked(0u, TransceiverClockRuns(mode));
        ArmFrameIrq();
    }

    /* MCIMX31RM §32.12.3.1: micro-frames are "timed precisely to 125 us using the transceiver
       clock"; §32.3.7: the MUX defaults to Serial, PTS 0b10 selects ULPI; Table 32-32 PHCD:
       "Writing this bit to a 1b will disable the PHY clock". ULPI 1.1 §3.2: the PHY provides it. */
    bool TransceiverClockRuns(FreescaleLowPowerMode mode) const {
        const uint32_t portsc = core_.Reg(Ci::kPortsc);
        if ((portsc & Ci::kPortscPtsMask) == Ci::kPortscPtsUlpi) return (portsc & Ci::kPortscPhcd) == 0u;
        return module_clocks_->ModuleRunsIn(FreescaleModule::kUsbotg, mode) && UpllRuns();
    }

    /* MCIMX31RM Table 32-25: SRE / FRE make the controller "issue an interrupt" while SRI /
       FRI are set. */
    void ArmFrameIrq() {
        const uint32_t intr    = core_.Reg(Ci::kUsbintr);
        const uint32_t enabled = intr & kFrameStatusBits;
        if ((frame_index_.Status(0u) & enabled) != 0u || (core_.Usbsts() & intr) != 0u || core_.OtgInterrupt()) {
            avic_->AssertSource(kAvicSourceUsbOtg);
            clock_->Disarm(irq_event_);
            return;
        }
        avic_->DeassertSource(kAvicSourceUsbOtg);
        const uint64_t frame_at = enabled != 0u ? frame_index_.NextStatusCycle(0u, enabled)
                                                : FreescaleUsbFrameIndex::kNever;
        const uint64_t at = std::min(frame_at, core_.NextOtgInterruptCycle());
        if (at == FreescaleUsbFrameIndex::kNever) clock_->Disarm(irq_event_);
        else                                      clock_->Arm(irq_event_, at);
    }

    FreescaleModuleClocks* module_clocks_ = nullptr;
    FreescaleTimerClocks*  timer_clocks_  = nullptr;
    Imx31Plls*             plls_          = nullptr;
    GuestCycleClock*       clock_         = nullptr;
    Imx31Avic*             avic_          = nullptr;
    GuestCycleClock::Event* irq_event_    = nullptr;
    FreescaleUsbFrameIndex frame_index_{emu_};
    std::array<uint32_t, ChipIdeaUsbCore::kBlockBytes / 4u> core_regs_{};
    UlpiPhy*        phy_ = nullptr;
    ChipIdeaUsbCore core_{emu_, core_regs_.data(), kOtgCore, nullptr};
    uint32_t usb_ctrl_ = 0;
};

}

REGISTER_SERVICE(Imx31Usbotg);

}
