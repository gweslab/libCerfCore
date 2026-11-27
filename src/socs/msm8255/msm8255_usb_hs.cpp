#include <cerfcore/peripherals/peripheral_base.h>

#include "msm8255_clock_reset.h"
#include "msm8255_id.h"
#include "msm8255_usb_phy.h"
#include "msm8255_usb_port.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../chipidea_usb_core.h"
#include "../irq_controller.h"
#include "../../core/deferred_jobs.h"
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <array>
#include <cstdint>
#include <mutex>

namespace cerfcore {

namespace {

using Ci = ChipIdeaUsbCore;

constexpr uint32_t kUsbHsBase = 0xA3600000u;
constexpr uint32_t kUsbHsSize = 0x00000400u;

constexpr uint32_t kUsbcmdItcMask        = 0x00FF0000u;
constexpr uint32_t kUsbcmdAsyncInterrupt = 1u << 29;
constexpr uint32_t kUsbcmdUlpiStop       = 1u << 30;
constexpr uint32_t kUsbcmdStored         = kUsbcmdItcMask | kUsbcmdAsyncInterrupt | kUsbcmdUlpiStop;

constexpr uint32_t kVicLine = 60u;

constexpr ChipIdeaUsbCoreKind kOtgCore{0x00080000u, 0u, false, {}};

constexpr uint32_t kUsbmodeEs = 1u << 2;

constexpr uint32_t kAhbBurst         = 0x090u;
constexpr uint32_t kAhbMode          = 0x098u;
constexpr uint32_t kAhbBurstAccepted = 0x00000007u;
constexpr uint32_t kAhbModeAccepted  = 0x0000000Fu;

constexpr uint32_t kCoreResetClock = 167u;
constexpr uint32_t kPhyResetClock  = 180u;

class Msm8255UsbHs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        clock_      = &emu_.Get<GuestCycleClock>();
        irq_        = &emu_.Get<IrqController>();
        port_       = &emu_.Get<Msm8255UsbPort>();
        jobs_       = &emu_.Get<DeferredJobs>();
        irq_event_  = clock_->Add([this] {
            std::lock_guard<std::mutex> lk(mtx_);
            RefreshIrq();
        });
        core_.Attach();
        ApplyAttachment();
        PowerOnController();
        phy_.PowerOn();
        clock_reset_ = &emu_.Get<Msm8255ClockReset>();
        clock_reset_->RegisterListener(kCoreResetClock, [this] {
            std::lock_guard<std::mutex> lk(mtx_);
            PowerOnController();
        });
        clock_reset_->RegisterListener(kPhyResetClock, [this] {
            std::lock_guard<std::mutex> lk(mtx_);
            phy_.PowerOn();
        });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            std::lock_guard<std::mutex> lk(mtx_);
            PowerOnController();
            phy_.PowerOn();
        });
        clock_->RegisterRateListener([this] {
            std::lock_guard<std::mutex> lk(mtx_);
            core_.Rescale();
            RefreshIrq();
        });
        port_->AddChangeListener([this] {
            jobs_->PostToGuestThread([this] {
                std::lock_guard<std::mutex> lk(mtx_);
                ApplyAttachment();
                RefreshIrq();
            });
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kUsbHsBase; }
    uint32_t MmioSize() const override { return kUsbHsSize; }

    uint32_t ReadWord(uint32_t addr) override {
        std::lock_guard<std::mutex> lk(mtx_);
        clock_reset_->RequireReleased(kCoreResetClock, addr);
        const uint32_t off = addr - kUsbHsBase;
        if (ChipIdeaUsbCore::IsEndptCtrl(off)) return core_.Reg(off);
        switch (off) {
        case ChipIdeaUsbCore::kUsbcmd:
        case ChipIdeaUsbCore::kListAddr:
        case ChipIdeaUsbCore::kUsbmode:
        case ChipIdeaUsbCore::kEndptPrime:
        case ChipIdeaUsbCore::kEndptStat:
        case ChipIdeaUsbCore::kUsbintr:
        case kAhbBurst:
        case kAhbMode:
            return core_.Reg(off);
        case ChipIdeaUsbCore::kPortsc:
            return core_.Portsc();
        case ChipIdeaUsbCore::kUsbsts:
            return core_.Usbsts();
        case ChipIdeaUsbCore::kOtgsc:
            return core_.Otgsc();
        case ChipIdeaUsbCore::kUlpiViewport: {
            clock_reset_->RequireReleased(kPhyResetClock, addr);
            uint32_t value = 0u;
            if (!core_.ReadViewport(value)) HaltUnsupportedAccess("ReadWord", addr, 0);
            return value;
        }
        default:
            HaltUnsupportedAccess("ReadWord", addr, 0);
        }
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        std::lock_guard<std::mutex> lk(mtx_);
        clock_reset_->RequireReleased(kCoreResetClock, addr);
        Write(addr, value);
        RefreshIrq();
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mtx_);
        w.WriteBytes("usbhs_regs", regs_.data(), sizeof(regs_));
        phy_.SaveState(w);
        core_.SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mtx_);
        r.ReadBytes("usbhs_regs", regs_.data(), sizeof(regs_));
        phy_.RestoreState(r);
        core_.RestoreState(r);
    }

    void PostRestore() override {
        std::lock_guard<std::mutex> lk(mtx_);
        ApplyAttachment();
        RefreshIrq();
    }

private:
    void Write(uint32_t addr, uint32_t value) {
        const uint32_t off = addr - kUsbHsBase;
        if (ChipIdeaUsbCore::IsEndptCtrl(off)) {
            core_.WriteEndptCtrl(ChipIdeaUsbCore::EndpointOf(off), value);
            return;
        }
        switch (off) {
        case ChipIdeaUsbCore::kUsbcmd:
            WriteUsbcmd(value);
            return;
        case ChipIdeaUsbCore::kUlpiViewport:
            clock_reset_->RequireReleased(kPhyResetClock, addr);
            if ((core_.Reg(ChipIdeaUsbCore::kUsbcmd) & kUsbcmdUlpiStop) != 0u) {
                emu_.Get<Fatal>().Die("msm8255 usb hs: ULPI viewport word 0x%08X while USBCMD blocks the PHY",
                                      value);
            }
            if (!core_.WriteViewport(value)) {
                emu_.Get<Fatal>().Die("msm8255 usb hs: ULPI viewport word 0x%08X names an operation the PHY cannot take",
                                      value);
            }
            return;
        case ChipIdeaUsbCore::kListAddr:
            core_.WriteListAddr(value);
            return;
        case ChipIdeaUsbCore::kUsbmode:
            if ((value & ChipIdeaUsbCore::kUsbmodeCm) == ChipIdeaUsbCore::kUsbmodeHost ||
                (value & kUsbmodeEs) != 0u) {
                HaltUnsupportedAccess("WriteWord", addr, value);
            }
            core_.WriteUsbmode(value);
            return;
        case ChipIdeaUsbCore::kEndptSetupStat:
            core_.ClearOnWrite(off, value);
            return;
        case ChipIdeaUsbCore::kUsbsts:
            core_.WriteUsbsts(value);
            return;
        case ChipIdeaUsbCore::kUsbintr:
            regs_[off >> 2] = value;
            return;
        case ChipIdeaUsbCore::kOtgsc:
            core_.WriteOtgsc(value);
            return;
        case kAhbBurst:
            if ((value & ~kAhbBurstAccepted) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
            regs_[off >> 2] = value;
            return;
        case kAhbMode:
            if ((value & ~kAhbModeAccepted) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
            regs_[off >> 2] = value;
            return;
        case Ci::kPortsc:
            if (core_.WritePortsc(value, nullptr) == ChipIdeaPortscWrite::kRejected) {
                HaltUnsupportedAccess("WriteWord", addr, value);
            }
            return;
        default:
            HaltUnsupportedAccess("WriteWord", addr, value);
        }
    }

    void PowerOnController() {
        core_.PowerOn();
        regs_[kAhbBurst >> 2] = 0u;
        regs_[kAhbMode >> 2]  = 0u;
        RefreshIrq();
    }

    void ApplyAttachment() {
        if (phy_.SetAttachment(port_->Attachment())) core_.ReceivePhyEvent();
    }

    void RefreshIrq() {
        const bool pending  = (core_.Usbsts() & core_.Reg(ChipIdeaUsbCore::kUsbintr)) != 0u || core_.OtgInterrupt();
        const bool phy_off  = (core_.Reg(ChipIdeaUsbCore::kPortsc) & ChipIdeaUsbCore::kPortscPhcd) != 0u;
        const bool async_on = (core_.Reg(ChipIdeaUsbCore::kUsbcmd) & kUsbcmdAsyncInterrupt) != 0u;
        const bool phy_int  = async_on && core_.OtgEventPending();
        if ((pending && (!phy_off || async_on)) || phy_int) irq_->AssertIrq(kVicLine);
        else                                                 irq_->DeAssertIrq(kVicLine);
        const uint64_t due = core_.NextOtgInterruptCycle();
        if (due == ChipIdeaUsbCore::kNever) clock_->Disarm(irq_event_);
        else                                clock_->Arm(irq_event_, due);
    }

    void WriteUsbcmd(uint32_t value) {
        const uint32_t now = core_.Reg(ChipIdeaUsbCore::kUsbcmd);
        if ((value & ~(ChipIdeaUsbCore::kUsbcmdReset | kUsbcmdStored)) != (now & ~kUsbcmdStored)) {
            emu_.Get<Fatal>().Die("msm8255 usb hs: USBCMD write 0x%08X over 0x%08X is not modeled",
                                  value, now);
        }
        core_.WriteUsbcmd(value);
        if (core_.ResetPending()) core_.ControllerReset();
    }

    std::mutex                                              mtx_;
    std::array<uint32_t, ChipIdeaUsbCore::kBlockBytes / 4u> regs_{};
    Msm8255UsbPhy           phy_;
    ChipIdeaUsbCore         core_{emu_, regs_.data(), kOtgCore, &phy_};
    Msm8255ClockReset*      clock_reset_ = nullptr;
    GuestCycleClock*        clock_       = nullptr;
    GuestCycleClock::Event* irq_event_   = nullptr;
    IrqController*          irq_         = nullptr;
    Msm8255UsbPort*         port_        = nullptr;
    DeferredJobs*           jobs_        = nullptr;
};

}

REGISTER_SERVICE(Msm8255UsbHs);

}
