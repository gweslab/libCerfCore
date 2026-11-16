#include "imx6_ipu.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include <mutex>
#include "imx6_ccm.h"
#include "imx6_ipu_cpmem.h"
#include "imx6_ipu_di_timing.h"
#include "imx6_ipu_display_signal.h"
#include "imx6_ipu_dp_srm_state.h"
#include "imx6_ipu_interrupt_controller.h"
#include "imx6_ipu_register_map.h"
#include "imx6_id.h"

namespace cerfcore {

using namespace imx6_ipu;

namespace {

constexpr uint32_t kConfDi0En = 1u << 6;
constexpr uint32_t kConfDi1En = 1u << 7;
constexpr uint32_t kConfDiEn  = kConfDi0En | kConfDi1En;

bool IsDiTimingRegister(uint32_t di_off) {
    return di_off <= 0x0004u || (di_off >= 0x000Cu && di_off <= 0x0054u);
}

const char* UnmeasuredRegisterName(uint32_t offset) {
    switch (offset & ~3u) {
    case kOffDpComConfSync: return "IPU_DP_COM_CONF_SYNC";
    case kOffDpGraphWindCtrlSync: return "IPU_DP_GRAPH_WIND_CTRL_SYNC";
    case kOffDpFgPosSync: return "IPU_DP_FG_POS_SYNC";
    default: return nullptr;
    }
}

}

REGISTER_SERVICE(Imx6Ipu);

bool Imx6Ipu::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && SocId::IsImx6(bd->GetSocId());
}

void Imx6Ipu::OnReady() {
    AttachScanClock();
    emu_.Get<Imx6Ccm>().RegisterRateListener([this] { OnScanSourceClockChange(); });
    Reset();
    emu_.Get<PeripheralDispatcher>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        Reset();
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        PostRestore();
        if (!emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) PostResetEffects();
    });
}

void Imx6Ipu::Reset() {
    std::lock_guard<std::mutex> lk(state_mtx_);
    StopScanLocked();
    regs_.assign(kCmSize / 4, 0u);
    regs_[kOffIdmacConf >> 2] = kRstIdmacConf;
    ResetDisplayBlockDefaults();
}

void Imx6Ipu::PostResetEffects() {
    emu_.Get<Imx6IpuDisplaySignal>().Reset();
    emu_.Get<Imx6IpuDpSrmState>().Reset();
    emu_.Get<Imx6IpuInterruptController>().PostReset();
    std::lock_guard<std::mutex> lk(state_mtx_);
    ResumeScanLocked(ScanNow());
}

uint32_t Imx6Ipu::MmioBase() const { return kCmBase; }
uint32_t Imx6Ipu::MmioSize() const { return kCmSize; }

uint8_t Imx6Ipu::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kCmBase;
    const uint32_t ipu_off = off;
    EnsureModelledRead("imx6-ipu read8 unmodelled register", addr, ipu_off);
    return static_cast<uint8_t>(ReadRegisterValue(ipu_off) >> ((off & 3u) * 8u));
}

uint16_t Imx6Ipu::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kCmBase;
    if (off & 1u) HaltUnsupportedAccess("ReadHalf misaligned", addr, 0);
    const uint32_t ipu_off = off;
    EnsureModelledRead("imx6-ipu read16 unmodelled register", addr, ipu_off);
    return static_cast<uint16_t>(ReadRegisterValue(ipu_off) >> ((off & 2u) * 8u));
}

uint32_t Imx6Ipu::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kCmBase;
    const uint32_t ipu_off = off;
    EnsureModelledRead("imx6-ipu read32 unmodelled register", addr, ipu_off);
    return ReadRegisterValue(ipu_off);
}

uint32_t Imx6Ipu::ReadRegisterValue(uint32_t ipu_off) const {
    if (IsIpuIntStat(ipu_off) || IsIpuIntCtrl(ipu_off))
        return emu_.Get<Imx6IpuInterruptController>().ReadRegister(ipu_off);
    uint32_t value = regs_[ipu_off >> 2];
    if (ipu_off == kOffIdmacBusy1) value = kIdmacChBusy1StubValue;
    if (ipu_off == kOffIdmacBusy2) value = kIdmacChBusy2StubValue;
    if (ipu_off == kOffDcStat) value = kDcStatResetValue;
    return value;
}

void Imx6Ipu::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kCmBase, sh = (off & 3u) * 8u;
    const uint32_t ipu_off = off;
    EnsureModelledWrite("imx6-ipu write8 unmodelled register", addr, ipu_off, value);
    uint32_t& w = regs_[ipu_off >> 2];
    WriteMerged(off, (w & ~(0xFFu << sh)) | (static_cast<uint32_t>(value) << sh), 0xFFu << sh);
}

void Imx6Ipu::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kCmBase;
    if (off & 1u) HaltUnsupportedAccess("WriteHalf misaligned", addr, value);
    const uint32_t sh = (off & 2u) * 8u;
    const uint32_t ipu_off = off;
    EnsureModelledWrite("imx6-ipu write16 unmodelled register", addr, ipu_off, value);
    uint32_t& w = regs_[ipu_off >> 2];
    WriteMerged(off, (w & ~(0xFFFFu << sh)) | (static_cast<uint32_t>(value) << sh), 0xFFFFu << sh);
}

void Imx6Ipu::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kCmBase;
    WriteMerged(off, value, 0xFFFFFFFFu);
}

uint32_t Imx6Ipu::ActiveDiBase() const {
    return (regs_[kOffConf >> 2] & kConfDi1En) != 0u ? kOffDi1Base : kOffDi0Base;
}

RasterScanPeripheral::ScanShape Imx6Ipu::ScanShapeLocked() const {
    const DiTiming t{&regs_[ActiveDiBase() >> 2]};
    const uint32_t di = ActiveDiBase() == kOffDi1Base ? 1u : 0u;
    auto& ccm = emu_.Get<Imx6Ccm>();
    const uint64_t input_hz = t.ExternalClock() ? ccm.IpuDiHz(di) : ccm.IpuHspHz();
    ScanShape s;
    s.tick              = {input_hz * 16u, t.ClockPeriod16()};
    s.frame.ticks       = t.FrameClocks();
    s.frame.edge[0]     = t.VsyncPreClocks();
    s.frame.edge[1]     = s.frame.ticks;
    s.frame.edges       = 2u;
    return s;
}

void Imx6Ipu::WriteConf(uint32_t value) {
    const uint32_t was = regs_[kOffConf >> 2] & kConfDiEn;
    regs_[kOffConf >> 2] = value;
    const uint32_t now_en = value & kConfDiEn;
    if (now_en != was) {
        std::lock_guard<std::mutex> lk(state_mtx_);
        StopScanLocked();
        if (now_en == kConfDiEn)
            emu_.Get<Fatal>().Die("[IPU] IPU_CONF 0x%08X enables both DIs; that scan-out is not modelled", value);
        if (now_en != 0u) {
            const DiTiming t{&regs_[ActiveDiBase() >> 2]};
            if (const char* why = t.Unmodelled())
                emu_.Get<Fatal>().Die("[IPU] IPU_CONF 0x%08X starts a DI with %s, which is not modelled", value, why);
            StartScanLocked(ScanNow());
            const ScanShape shape = ScanShapeLocked();
            LOG(Lcd, "[IPU] DI%u scan: %llu/%llu Hz display clock, %llu clocks per frame\n",
                now_en == kConfDi1En ? 1u : 0u, static_cast<unsigned long long>(shape.tick.num),
                static_cast<unsigned long long>(shape.tick.den),
                static_cast<unsigned long long>(shape.frame.ticks));
        }
    }
    emu_.Get<Imx6IpuDisplaySignal>().Refresh();
}

void Imx6Ipu::WriteDi(uint32_t ipu_off, uint32_t value) {
    const uint32_t old = regs_[ipu_off >> 2];
    regs_[ipu_off >> 2] = value;
    const bool active_di = (ipu_off & ~0x1FFFu) == ActiveDiBase() &&
                           (regs_[kOffConf >> 2] & kConfDiEn) != 0u;
    if (active_di && old != value && IsDiTimingRegister(ipu_off & 0x1FFFu))
        emu_.Get<Fatal>().Die("[IPU] DI timing write 0x%08X at +0x%05X while the DI runs is not modelled",
                              value, ipu_off);
    emu_.Get<Imx6IpuDisplaySignal>().Refresh();
}

void Imx6Ipu::FrameEdgeLocked(uint32_t edge_index) {
    if (edge_index == 0u) {
        emu_.Get<Imx6IpuInterruptController>().Raise(ActiveDiBase() == kOffDi1Base ? kIpuIrqVsyncPre1
                                                                                  : kIpuIrqVsyncPre0);
        return;
    }
    if (edge_index != 1u)
        emu_.Get<Fatal>().Die("[IPU] scan event edge %u is not modelled", edge_index);

    if (dp_srm_update_pending_) {
        emu_.Get<Imx6IpuDpSrmState>().UpdateFromSrm();
        dp_srm_update_pending_ = false;
        emu_.Get<Imx6IpuDisplaySignal>().Refresh();
    }

    auto& cp = emu_.Get<Imx6IpuCpmem>();
    const uint32_t ch = emu_.Get<Imx6IpuDisplaySignal>().ActiveDisplayChannel();
    if (ch >= 64u || !EnabledMaskForChannel(ch)) return;

    const uint32_t bit = 1u << (ch & 31u);
    const uint32_t db_mode = kOffChDbModeSel0 + 4u * (ch / 32u);
    const uint32_t ready0 = kOffChaBuf0Rdy0 + 4u * (ch / 32u);
    const uint32_t ready1 = kOffChaBuf1Rdy0 + 4u * (ch / 32u);
    const uint32_t ready2 = ch < 32u ? kOffChaBuf2Rdy0 : kOffChaBuf2Rdy1;
    const bool r0 = (regs_[ready0 >> 2] & bit) != 0u;
    const bool r1 = (regs_[ready1 >> 2] & bit) != 0u;
    if ((regs_[ready2 >> 2] & bit) != 0u)
        emu_.Get<Fatal>().Die("[IPU] channel %u uses triple buffering, which is not modelled", ch);

    const bool double_buffered = (regs_[db_mode >> 2] & bit) != 0u;
    if ((!r0 && !r1) || (!double_buffered && !r0)) return;

    const uint32_t cur = kOffChaCurBuf0 + 4u * (ch / 32u);
    const bool use_buf1 = r1 && (!r0 || (regs_[cur >> 2] & bit) == 0u);
    if (use_buf1) {
        regs_[cur >> 2] |= bit;
        regs_[ready1 >> 2] &= ~bit;
    } else {
        regs_[cur >> 2] &= ~bit;
        regs_[ready0 >> 2] &= ~bit;
    }
    cp.SetCurrentBuffer(ch, use_buf1 ? 1u : 0u);

    emu_.Get<Imx6IpuInterruptController>().Raise(ch);
}

void Imx6Ipu::WriteMerged(uint32_t off, uint32_t value, uint32_t write_mask) {
    const uint32_t ipu_off = off;
    EnsureModelledWrite("imx6-ipu write unmodelled register", kCmBase + off, ipu_off, value);
    if (IsIpuIntStat(ipu_off)) {
        emu_.Get<Imx6IpuInterruptController>().ClearStatus(ipu_off, value & write_mask);
        return;
    }
    if (IsIpuIntCtrl(ipu_off)) {
        auto& interrupts = emu_.Get<Imx6IpuInterruptController>();
        const uint32_t old_control = interrupts.ReadRegister(ipu_off);
        interrupts.WriteControl(ipu_off, (old_control & ~write_mask) | (value & write_mask));
        return;
    }
    const uint32_t old = regs_[ipu_off >> 2];
    value = (old & ~write_mask) | (value & write_mask);
    if (ipu_off == kOffSrmPri2 && (write_mask & 0x00000018u) != 0u) {
        regs_[ipu_off >> 2] = value;
        const uint32_t mode = (value >> 3) & 3u;
        if (mode == 0u) {
            dp_srm_update_pending_ = false;
        } else if (mode == 1u) {
            dp_srm_update_pending_ = true;
        } else if (mode == 2u) {
            emu_.Get<Fatal>().Die("[IPU] SRM_PRI2 DP_S_SRM_MODE encoding 10 is reserved");
        } else {
            dp_srm_update_pending_ = false;
            emu_.Get<Imx6IpuDpSrmState>().UpdateFromSrm();
            emu_.Get<Imx6IpuDisplaySignal>().Refresh();
        }
        return;
    }
    if (ipu_off == kOffMemRst) {
        const uint32_t enabled_memories = value & 0x007FFFFFu;
        constexpr uint32_t kGuestV13MemoryEnables = 0x007007FFu;
        const uint32_t unsupported_memories = enabled_memories & ~kGuestV13MemoryEnables;
        static constexpr const char* kMemoryNames[] = {
            "srm", "alpha", "cpmem", "tpm", "mpm", "bm", "rm", "dstm", "dsom", "lut0", "lut1", "ram_smfc",
            "vdi_fifo2", "vdi_fifo3", "icb", "vdi_fifo1", "reserved bit 16", "reserved bit 17", "reserved bit 18", "reserved bit 19", "dc_template",
            "dmfc_rd", "dmfc_wr"};
        for (uint32_t bit = 0; bit < 23u; ++bit) {
            if ((unsupported_memories & (1u << bit)) != 0u)
                emu_.Get<Fatal>().Die("[IPU] IPUx_MEM_RST sets unsupported RST_MEM_EN %s", kMemoryNames[bit]);
        }
        if ((value & 0x7F800000u) != 0u)
            emu_.Get<Fatal>().Die("[IPU] IPUx_MEM_RST writes reserved bits 30:23 (0x%08X)", value);
        if ((value & kRstMemStart) != 0u)
            emu_.Get<Imx6IpuCpmem>().ResetMemories(enabled_memories);
        if ((value & kRstMemStart) != 0u && (enabled_memories & (1u << 2)) != 0u)
            emu_.Get<Imx6IpuDisplaySignal>().ResetActiveDisplayChannel();
        regs_[ipu_off >> 2] = value & ~kRstMemStart;
        return;
    }
    if (ipu_off == kOffGpr) {
        regs_[ipu_off >> 2] = value;
        return;
    }
    if (IsIcOff(ipu_off)) {
        if (ipu_off == kOffIcBase &&
            ((value & 0xC0000000u) != 0x40000000u || (value & 0x3FFFFFFFu) != (old & 0x3FFFFFFFu)))
            emu_.Get<Fatal>().Die("[IPU IC] IPU_IC_CONF write 0x%08X changes unmodelled bits 29:0 or task mode",
                                  value);
        if (ipu_off == kOffIcBase + 0x18u) {
            const uint32_t old_value = regs_[ipu_off >> 2];
            const uint32_t cleared = old_value & ~value;
            if ((value & ~old_value) != 0u || (cleared & ~kIcIdmac1GuestClearMask) != 0u)
                emu_.Get<Fatal>().Die("[IPU IC] IPU_IC_IDMAC_1 write 0x%08X is not modelled", value);
        }
        if (ipu_off == kOffIcBase + 0x24u && value != kIcIdmac4GuestValue)
            emu_.Get<Fatal>().Die("[IPU IC] IPU_IC_IDMAC_4 write 0x%08X is not modelled", value);
        regs_[ipu_off >> 2] = value;
        return;
    }
    if (IsIpuCurBuf(ipu_off)) {
        const uint32_t written = value & write_mask;
        regs_[ipu_off >> 2] &= ~written;
        UpdateCpmemCurrentBuffer(ipu_off, written, 0u);
        return;
    }
    if (IsIpuBufReady(ipu_off)) {
        const uint32_t written = value & write_mask;
        uint32_t clear_bit = 28u;
        if (ipu_off == kOffChaBuf0Rdy0 + 4u) clear_bit = 29u;
        else if (ipu_off == kOffChaBuf1Rdy0) clear_bit = 30u;
        else if (ipu_off == kOffChaBuf1Rdy0 + 4u) clear_bit = 31u;
        if ((regs_[kOffGpr >> 2] & (1u << clear_bit)) != 0u)
            regs_[ipu_off >> 2] &= ~written;
        else
            regs_[ipu_off >> 2] |= written;
        emu_.Get<Imx6IpuDisplaySignal>().Refresh();
        return;
    }
    if (IsIpuBuf2Ready(ipu_off)) {
        const uint32_t written = value & write_mask;
        const uint32_t clear_bit = ipu_off == kOffChaBuf2Rdy0 ? 20u : 21u;
        if ((regs_[kOffGpr >> 2] & (1u << clear_bit)) != 0u)
            regs_[ipu_off >> 2] &= ~written;
        else
            regs_[ipu_off >> 2] |= written;
        if (regs_[ipu_off >> 2] != 0u)
            emu_.Get<Fatal>().Die("[IPU] triple buffering is not modelled (BUF2_RDY mask=0x%08X)",
                                  regs_[ipu_off >> 2]);
        return;
    }
    if (IsVdiOff(ipu_off)) {
        const uint32_t changed = regs_[ipu_off >> 2] ^ value;
        const uint32_t unmodelled = changed & ~kVdiGuestFieldMask;
        if (ipu_off != kOffVdiC || unmodelled != 0u)
            emu_.Get<Fatal>().Die("[IPU VDI] register write 0x%08X at +0x%05X changes unmodelled bits 0x%08X "
                                  "(old=0x%08X allowed=0x%08X)", value, ipu_off, unmodelled,
                                  regs_[ipu_off >> 2], kVdiGuestFieldMask);
        regs_[ipu_off >> 2] = value;
        return;
    }
    if (ipu_off == kOffIdmacChEn1) {
        const uint32_t display_mask = DisplayChannelMask();
        const uint32_t was = regs_[ipu_off >> 2];
        regs_[ipu_off >> 2] = value;
        if (((was ^ value) & display_mask) != 0u) {
            emu_.Get<Imx6IpuDisplaySignal>().Refresh();
        }
        return;
    }
    if (ipu_off == kOffIdmacChEn2) {
        const uint32_t display_mask = DisplayChannelMaskHigh();
        const uint32_t was = regs_[ipu_off >> 2];
        regs_[ipu_off >> 2] = value;
        if (((was ^ value) & display_mask) != 0u) {
            emu_.Get<Imx6IpuDisplaySignal>().Refresh();
        }
        return;
    }
    if (IsDmfcOff(ipu_off)) {
        regs_[ipu_off >> 2] = value;
        emu_.Get<Imx6IpuDisplaySignal>().Refresh();
        return;
    }
    if (IsDcOff(ipu_off)) {
        if (ipu_off == kOffDcWrChConf1 || ipu_off == kOffDcWrChConf5 || ipu_off == kOffDcWrChConf6) {
            const char* name = ipu_off == kOffDcWrChConf1 ? "DC_WR_CH_CONF_1"
                               : ipu_off == kOffDcWrChConf5 ? "DC_WR_CH_CONF_5"
                                                            : "DC_WR_CH_CONF_6";
            const char* mode_name = ipu_off == kOffDcWrChConf1 ? "PROG_CHAN_TYP_1"
                                    : ipu_off == kOffDcWrChConf5 ? "PROG_CHAN_TYP_5"
                                                                 : "PROG_CHAN_TYP_6";
            const uint32_t channel_mode = (value >> 5) & 0x7u;
            if (channel_mode != 0u && channel_mode != 4u)
                emu_.Get<Fatal>().Die("[IPU DC] %s %s mode %u is not modelled", name, mode_name, channel_mode);
            if (value != (0x2u | (channel_mode << 5)))
                emu_.Get<Fatal>().Die("[IPU DC] %s value 0x%08X has unmodelled fields", name, value);
        }
        regs_[ipu_off >> 2] = value;
        emu_.Get<Imx6IpuDisplaySignal>().Refresh();
        return;
    }
    if (IsDiOff(ipu_off)) {
        WriteDi(ipu_off, value);
        return;
    }
    if (ipu_off == kOffConf) {
        WriteConf(value);
        return;
    }
    regs_[ipu_off >> 2] = value;
    if (ipu_off == kOffFsDispFlow1 || ipu_off == kOffFsDispFlow2)
        emu_.Get<Imx6IpuDisplaySignal>().Refresh();
}

void Imx6Ipu::EnsureModelledRead(const char* what, uint32_t addr, uint32_t ipu_off) const {
    if (IsModelledRead(ipu_off)) return;
    if (const char* name = UnmeasuredRegisterName(ipu_off))
        emu_.Get<Fatal>().Die("[IPU] guest read of unmeasured register %s at 0x%08X", name, addr);
    HaltUnsupportedAccess(what, addr, 0);
}

void Imx6Ipu::EnsureModelledWrite(const char* what, uint32_t addr, uint32_t ipu_off, uint32_t value) const {
    if (IsModelledWrite(ipu_off)) return;
    if (const char* name = UnmeasuredRegisterName(ipu_off))
        emu_.Get<Fatal>().Die("[IPU] guest write of unmeasured register %s at 0x%08X", name, addr);
    HaltUnsupportedAccess(what, addr, value);
}

void Imx6Ipu::ResetDisplayBlockDefaults() {
    regs_[kOffSrmPri1 >> 2] = kRstSrmPri1;
    regs_[kOffSrmPri2 >> 2] = kRstSrmPri2;
    dp_srm_update_pending_ = false;
    regs_[kOffDispGen >> 2] = kRstDispGen;
    regs_[kOffDispAlt1 >> 2] = kRstDispAlt1;
    regs_[kOffDispAlt2 >> 2] = kRstDispAlt2;
    regs_[kOffDispAlt4 >> 2] = kRstDispAlt4;
    regs_[kOffPm >> 2] = kRstPm;
    regs_[kOffDi0General >> 2] = kRstDi0General;
    regs_[kOffDmfcRdChan >> 2] = kRstDmfcRdChan;
    regs_[kOffDmfcWrChan >> 2] = kRstDmfcWrChan;
    regs_[kOffDmfcDpChan >> 2] = 0x00000000u;
    regs_[kOffDmfcWrChanDef >> 2] = 0x20202020u;
    regs_[kOffDmfcDpChanDef >> 2] = 0x20202020u;
    regs_[kOffDmfcGeneral1 >> 2] = 0x00000003u;
}

void Imx6Ipu::SaveState(StateWriter& w) {
    w.WriteBytes("regs", regs_.data(), regs_.size() * sizeof(uint32_t));
    w.Write<uint8_t>("dp_srm_update_pending", dp_srm_update_pending_ ? 1u : 0u);
    emu_.Get<Imx6IpuDisplaySignal>().SaveState(w);
    emu_.Get<Imx6IpuDpSrmState>().SaveState(w);
    emu_.Get<Imx6IpuInterruptController>().SaveState(w);
    std::lock_guard<std::mutex> lk(state_mtx_);
    SaveScanLocked(w);
}

void Imx6Ipu::RestoreState(StateReader& r) {
    r.ReadBytes("regs", regs_.data(), regs_.size() * sizeof(uint32_t));
    uint8_t dp_srm_update_pending = 0u;
    r.Read("dp_srm_update_pending", dp_srm_update_pending);
    dp_srm_update_pending_ = dp_srm_update_pending != 0u;
    emu_.Get<Imx6IpuDisplaySignal>().RestoreState(r);
    emu_.Get<Imx6IpuDpSrmState>().RestoreState(r);
    emu_.Get<Imx6IpuInterruptController>().RestoreState(r);
    std::lock_guard<std::mutex> lk(state_mtx_);
    RestoreScanLocked(r);
}

void Imx6Ipu::PostRestore() {
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        ResumeScanLocked(ScanNow());
    }
    emu_.Get<Imx6IpuInterruptController>().PostRestore();
    emu_.Get<Imx6IpuDisplaySignal>().Refresh();
}

uint32_t Imx6Ipu::EnabledMaskForChannel(uint32_t ch) const {
    const uint32_t reg = (ch < 32u) ? kOffIdmacChEn1 : kOffIdmacChEn2;
    return regs_[reg >> 2] & (1u << (ch & 31u));
}

bool Imx6Ipu::IdmacChannelEnabled(uint32_t ch) const {
    return ch < 64u && EnabledMaskForChannel(ch) != 0u;
}

bool Imx6Ipu::IdmacChannelReady(uint32_t ch) const {
    return ch < 64u && ReadyMaskForChannel(ch) != 0u;
}

uint32_t Imx6Ipu::ReadyMaskForChannel(uint32_t ch) const {
    const uint32_t buf0 = kOffChaBuf0Rdy0 + 4u * (ch / 32u);
    const uint32_t buf1 = kOffChaBuf1Rdy0 + 4u * (ch / 32u);
    return (regs_[buf0 >> 2] | regs_[buf1 >> 2]) & (1u << (ch & 31u));
}

uint32_t Imx6Ipu::RegisterValue(uint32_t offset) const { return regs_[offset >> 2]; }

void Imx6Ipu::UpdateCpmemCurrentBuffer(uint32_t ipu_off, uint32_t mask, uint32_t buffer) {
    auto& cp = emu_.Get<Imx6IpuCpmem>();
    const uint32_t bank = ipu_off == kOffChaCurBuf0 + 4u ? 32u : 0u;
    for (uint32_t bit = 0; bit < 32u; ++bit) {
        if (mask & (1u << bit)) cp.SetCurrentBuffer(bank + bit, buffer);
    }
}

}
