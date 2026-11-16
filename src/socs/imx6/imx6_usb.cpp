#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "../freescale_usb_frame_index.h"
#include "imx6_mmio_lane.h"

#include <algorithm>
#include <cstdint>
#include "imx6_id.h"

namespace cerfcore {

namespace {

class Imx6Usb : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        frame_index_.Attach();
        ResetRegisters();
        emu_.Get<GuestCycleClock>().RegisterRateListener([this] { frame_index_.Rescale(); });
        RefreshFrameClocks();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            ResetRegisters();
        });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            RefreshFrameClocks();
        });
    }

    uint32_t MmioBase() const override { return 0x02184000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usb read32 unaligned", addr, 0);
        if (off >= kNonCore) {
            if (!IsUsbncOffset(off)) HaltUnsupportedAccess("imx6-usb read32 unmodelled register", addr, 0);
            return regs_[off >> 2];
        }
        const uint32_t core = off / kCoreSpan;
        const uint32_t coff = off % kCoreSpan;
        if (!IsCoreRegister(core, coff)) HaltUnsupportedAccess("imx6-usb read32 unmodelled register", addr, 0);
        if (coff == kOffUsbsts) return UsbStatus(core);
        if (coff == kOffFrindex) return frame_index_.Frindex(core);
        return regs_[off >> 2];
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usb write32 unaligned", addr, value);
        if (off >= kNonCore) {
            if (!IsUsbncOffset(off)) HaltUnsupportedAccess("imx6-usb write32 unmodelled register", addr, value);
            regs_[off >> 2] = value;
            return;
        }
        const uint32_t core = off / kCoreSpan;
        const uint32_t coff = off % kCoreSpan;
        if (!IsCoreRegister(core, coff)) HaltUnsupportedAccess("imx6-usb write32 unmodelled register", addr, value);
        uint32_t& reg = regs_[off >> 2];
        switch (coff) {
        case kOffCaplen:
        case kOffHcsparams:
        case kOffHccparams:
        case kOffDciversion:
        case kOffDccparams:
            HaltUnsupportedAccess("imx6-usb write to a read-only capability register", addr, value);
        case kOffUsbcmd:
            if ((value & kCmdRst) != 0u) {
                if (Running(core)) HaltUnsupportedAccess("imx6-usb RST of a running controller", addr, value);
                ControllerResetCore(core);
                return;
            }
            if ((value & kCmdIaa) != 0u)
                HaltUnsupportedAccess("imx6-usb async advance doorbell", addr, value);
            reg = value;
            frame_index_.SetFrameListSize(core, FreescaleUsbFrameIndex::FrameListSizeCode(value));
            frame_index_.SetRunning(core, (value & kCmdRs) != 0u, FrameRole(core));
            CheckRunningInterrupts(core, addr);
            return;
        case kOffUsbsts:
            reg &= ~(value & kStsW1c);
            frame_index_.ClearStatus(core, value & kFrameStatusBits);
            return;
        case kOffUsbintr:
            reg = value;
            CheckRunningInterrupts(core, addr);
            return;
        case kOffFrindex:
            if (Running(core)) HaltUnsupportedAccess("imx6-usb FRINDEX write while running", addr, value);
            frame_index_.WriteFrindex(core, value);
            return;
        case kOffPortsc1:
            WritePortsc(core, addr, value);
            RefreshFrameClock(core);
            return;
        case kOffOtgsc:
            WriteOtgsc(core, addr, value);
            return;
        case kOffUsbmode:
            if ((value & kModeCmMask) == kModeCmDevice)
                HaltUnsupportedAccess("imx6-usb device controller mode", addr, value);
            reg = value;
            return;
        case kOffUlpiview:
            HaltUnsupportedAccess("imx6-usb ULPI viewport", addr, value);
        default:
            reg = value;
            return;
        }
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes("regs", regs_, sizeof(regs_));
        frame_index_.Save(w);
    }

    void RestoreState(StateReader& r) override {
        r.ReadBytes("regs", regs_, sizeof(regs_));
        frame_index_.Restore(r);
    }

private:
    void ResetRegisters() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        for (uint32_t core = 0; core < kCoreCount; ++core) {
            const uint32_t base = core * kCoreSpan;
            regs_[(base + kOffCaplen) >> 2] = 0x01000040u;
            regs_[(base + kOffHcsparams) >> 2] = 0x00010011u;
            regs_[(base + kOffHccparams) >> 2] = 0x00000006u;
            if (core == 0u) {
                regs_[(base + kOffDciversion) >> 2] = 0x00000001u;
                regs_[(base + kOffDccparams) >> 2] = 0x00000188u;
            }
            PowerOnResetCore(core);
        }
        regs_[(kNonCore + 0x00u) >> 2] = 0x00003000u;
        regs_[(kNonCore + 0x04u) >> 2] = 0x00003000u;
        regs_[(kNonCore + 0x08u) >> 2] = 0x00001000u;
        regs_[(kNonCore + 0x0Cu) >> 2] = 0x00001000u;
        regs_[(kNonCore + 0x10u) >> 2] = 0x00000042u;
        regs_[(kNonCore + 0x14u) >> 2] = 0x00000042u;
        regs_[(kNonCore + 0x18u) >> 2] = 0x00000000u;
        regs_[(kNonCore + 0x1Cu) >> 2] = 0x00000098u;
    }

    void RefreshFrameClocks() {
        for (uint32_t core = 0; core < kCoreCount; ++core) RefreshFrameClock(core);
    }

    static constexpr uint32_t kCoreSpan = 0x200u;
    static constexpr uint32_t kCoreCount = 4u;
    static constexpr uint32_t kNonCore = 0x800u;

    static constexpr uint32_t kOffCaplen = 0x100u;
    static constexpr uint32_t kOffHcsparams = 0x104u;
    static constexpr uint32_t kOffHccparams = 0x108u;
    static constexpr uint32_t kOffDciversion = 0x120u;
    static constexpr uint32_t kOffDccparams = 0x124u;
    static constexpr uint32_t kOffUsbcmd = 0x140u;
    static constexpr uint32_t kOffUsbsts = 0x144u;
    static constexpr uint32_t kOffUsbintr = 0x148u;
    static constexpr uint32_t kOffFrindex = 0x14Cu;
    static constexpr uint32_t kOffCtrlDsSeg = 0x150u;
    static constexpr uint32_t kOffPeriodicList = 0x154u;
    static constexpr uint32_t kOffAsyncList = 0x158u;
    static constexpr uint32_t kOffBurstSize = 0x160u;
    static constexpr uint32_t kOffTxFill = 0x164u;
    static constexpr uint32_t kOffUlpiview = 0x170u;
    static constexpr uint32_t kOffConfigFlg = 0x180u;
    static constexpr uint32_t kOffPortsc1 = 0x184u;
    static constexpr uint32_t kOffOtgsc = 0x1A4u;
    static constexpr uint32_t kOffUsbmode = 0x1A8u;

    static constexpr uint32_t kCmdRs = 1u << 0;
    static constexpr uint32_t kCmdRst = 1u << 1;
    static constexpr uint32_t kCmdPse = 1u << 4;
    static constexpr uint32_t kCmdAse = 1u << 5;
    static constexpr uint32_t kCmdIaa = 1u << 6;
    static constexpr uint32_t kStsHch = 1u << 12;
    static constexpr uint32_t kStsPs = 1u << 14;
    static constexpr uint32_t kStsAs = 1u << 15;
    static constexpr uint32_t kStsW1c = 0x030007FFu;
    static constexpr uint32_t kIntrFrameEvents = (1u << 3) | (1u << 7);
    static constexpr uint32_t kPortW1c = 0x0000002Au;
    static constexpr uint32_t kPortRo = 0x0C000E11u;
    static constexpr uint32_t kPortPe = 1u << 2;
    static constexpr uint32_t kPortUnmodelled = 0x000F2140u;
    static constexpr uint32_t kOtgscW1c = 0x007F0000u;
    static constexpr uint32_t kOtgscRo = 0x00007F00u;
    static constexpr uint32_t kOtgsc1msE = 1u << 29;
    static constexpr uint32_t kPortKeptOnRst = 0xF2F0D000u;
    static constexpr uint32_t kPortPhcd = 1u << 23;
    static constexpr uint32_t kFrameStatusBits = FreescaleUsbFrameIndex::kStsFri | FreescaleUsbFrameIndex::kStsSri;
    static constexpr uint32_t kModeCmMask = 3u;
    static constexpr uint32_t kModeCmDevice = 2u;
    static constexpr uint32_t kModeCmHost = 3u;

    static bool IsUsbncOffset(uint32_t off) { return off >= kNonCore && off <= 0x81Cu && (off & 3u) == 0u; }

    static bool IsCoreRegister(uint32_t core, uint32_t coff) {
        switch (coff) {
        case kOffCaplen:
        case kOffHcsparams:
        case kOffHccparams:
        case kOffUsbcmd:
        case kOffUsbsts:
        case kOffUsbintr:
        case kOffFrindex:
        case kOffCtrlDsSeg:
        case kOffPeriodicList:
        case kOffAsyncList:
        case kOffBurstSize:
        case kOffTxFill:
        case kOffUlpiview:
        case kOffConfigFlg:
        case kOffPortsc1:
        case kOffUsbmode: return true;
        case kOffDciversion:
        case kOffDccparams: return core == 0u;
        case kOffOtgsc: return true;
        default: return false;
        }
    }

    uint32_t& Reg(uint32_t core, uint32_t coff) { return regs_[(core * kCoreSpan + coff) >> 2]; }
    uint32_t Reg(uint32_t core, uint32_t coff) const { return regs_[(core * kCoreSpan + coff) >> 2]; }

    bool HostMode(uint32_t core) const { return (Reg(core, kOffUsbmode) & kModeCmMask) == kModeCmHost; }
    FreescaleUsbFrameIndex::Role FrameRole(uint32_t core) const {
        return HostMode(core) ? FreescaleUsbFrameIndex::Role::kHost : FreescaleUsbFrameIndex::Role::kDeviceHighSpeed;
    }
    bool Running(uint32_t core) const { return (Reg(core, kOffUsbcmd) & kCmdRs) != 0u; }

    uint32_t UsbStatus(uint32_t core) {
        const uint32_t cmd = Reg(core, kOffUsbcmd);
        uint32_t s = Reg(core, kOffUsbsts) & ~(kStsHch | kStsAs | kStsPs);
        if ((cmd & kCmdRs) == 0u && (core != 0u || HostMode(core))) s |= kStsHch;
        if ((cmd & kCmdAse) != 0u) s |= kStsAs;
        if ((cmd & kCmdPse) != 0u) s |= kStsPs;
        return s | frame_index_.Status(core);
    }

    void PowerOnResetCore(uint32_t core) {
        Reg(core, kOffPortsc1) = 0x10000000u;
        Reg(core, kOffOtgsc) = 0x00000120u;
        ControllerResetCore(core);
    }

    void ControllerResetCore(uint32_t core) {
        Reg(core, kOffUsbcmd) = 0x00080000u;
        Reg(core, kOffUsbsts) = 0u;
        Reg(core, kOffUsbintr) = 0u;
        Reg(core, kOffFrindex) = 0u;
        Reg(core, kOffCtrlDsSeg) = 0u;
        Reg(core, kOffPeriodicList) = 0u;
        Reg(core, kOffAsyncList) = 0u;
        Reg(core, kOffBurstSize) = 0u;
        Reg(core, kOffTxFill) = 0x00000808u;
        Reg(core, kOffUlpiview) = 0u;
        Reg(core, kOffConfigFlg) = 0x00000001u;
        Reg(core, kOffPortsc1) = (Reg(core, kOffPortsc1) & kPortKeptOnRst) | (0x10000000u & ~kPortKeptOnRst);
        Reg(core, kOffUsbmode) = 0u;
        frame_index_.Reset(core);
        frame_index_.SetRunning(core, false, FreescaleUsbFrameIndex::Role::kDeviceHighSpeed);
    }

    void RefreshFrameClock(uint32_t core) {
        frame_index_.SetClocked(core, (Reg(core, kOffPortsc1) & kPortPhcd) == 0u);
    }

    void CheckRunningInterrupts(uint32_t core, uint32_t addr) {
        if (Running(core) && (Reg(core, kOffUsbintr) & kIntrFrameEvents) != 0u)
            HaltUnsupportedAccess("imx6-usb frame-list rollover or SOF interrupt of a running controller",
                                  addr, Reg(core, kOffUsbintr));
    }

    void WritePortsc(uint32_t core, uint32_t addr, uint32_t value) {
        if ((value & kPortUnmodelled) != 0u)
            HaltUnsupportedAccess("imx6-usb PORTSC1 port reset, resume, owner or test control", addr, value);
        uint32_t& reg = Reg(core, kOffPortsc1);
        const uint32_t old = reg;
        const uint32_t pe = old & value & kPortPe;
        reg = (old & kPortRo) | (old & kPortW1c & ~value) | pe | (value & ~(kPortRo | kPortW1c | kPortPe));
    }

    void WriteOtgsc(uint32_t core, uint32_t addr, uint32_t value) {
        if ((value & kOtgsc1msE) != 0u)
            HaltUnsupportedAccess("imx6-usb OTGSC 1 ms timer interrupt", addr, value);
        uint32_t& reg = Reg(core, kOffOtgsc);
        const uint32_t old = reg;
        reg = (old & kOtgscRo) | (old & kOtgscW1c & ~value) | (value & ~(kOtgscRo | kOtgscW1c));
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        const uint32_t coff = off < kNonCore ? off % kCoreSpan : off;
        if (off < kNonCore && (coff == kOffUsbsts || coff == kOffUsbcmd || coff == kOffPortsc1 ||
                               coff == kOffOtgsc)) {
            HaltUnsupportedAccess("imx6-usb narrow write to a register with side effects", lane.address,
                                  lane.value);
        }
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

    uint32_t regs_[0x4000u / 4u]{};
    FreescaleUsbFrameIndex frame_index_{emu_};
};
}

REGISTER_SERVICE(Imx6Usb);

}
