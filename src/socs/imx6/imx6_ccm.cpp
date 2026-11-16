#include "imx6_ccm.h"

#include "imx6_anatop.h"
#include "imx6_boot_display_clock.h"
#include "imx6_display_clock_input.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/state/state_stream.h>
#include "imx6_id.h"
#include "imx6_variant.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kCbcdr   = 0x14u;
constexpr uint32_t kCacrr   = 0x10u;
constexpr uint32_t kCacrrArmPodf = 0x7u;
constexpr uint32_t kCbcmr   = 0x18u;
constexpr uint32_t kCcr     = 0x00u;
constexpr uint32_t kCcmGuestOffset50Stub = 0x50u;
constexpr uint32_t kCcrRbcEn = 1u << 27;
constexpr uint32_t kCcrRegBypassCount = 0x3Fu << 21;
constexpr uint32_t kCcrWbCount = 7u << 16;
constexpr uint32_t kCcrCoscEn = 1u << 12;
constexpr uint32_t kCcrOscnt = 0xFFu;
constexpr uint32_t kCcrGuestWritten = kCcrRbcEn | kCcrRegBypassCount | kCcrWbCount;
constexpr uint32_t kCscmr2  = 0x20u;
constexpr uint32_t kCs2cdr  = 0x2Cu;
constexpr uint32_t kCs2cdrLdbDi1SelMask = 7u << 12;
constexpr uint32_t kCscmr2LdbDi1IpuDivMask = 1u << 11;
constexpr uint32_t kChsccdr = 0x34u;
constexpr uint32_t kChsccdrReset = 0x0002A150u;
constexpr uint32_t kChsccdrDiFieldMask = 0x1FFu;
constexpr uint32_t kChsccdrDiClkSelMask = 0x7u;
constexpr uint32_t kChsccdrDiPodfMask = 0x38u;
constexpr uint32_t kChsccdrDiPreClkSelMask = 0x1C0u;
constexpr uint32_t kChsccdrPll5PreClkSel = 2u;
constexpr uint32_t kChsccdrPll3Pfd1PreClkSel = 5u;
constexpr uint32_t kCscdr1  = 0x24u;
constexpr uint32_t kCscdr3  = 0x3Cu;
constexpr uint32_t kCcgr0   = 0x68u;
constexpr uint32_t kCcgr6   = 0x80u;
constexpr uint32_t kClpcr   = 0x54u;

constexpr uint32_t kClpcrReset = 0x00000079u;
constexpr uint32_t kClpcrLpm = 3u;
constexpr uint32_t kClpcrGuestWritten = (1u << 5) | (1u << 6) | (1u << 8) |
                                         (3u << 9) | (1u << 16) | (1u << 21);
constexpr uint32_t kClpcrStoredMask = kClpcrLpm | kClpcrGuestWritten;
constexpr uint32_t kCcmGuestOffset50StoredMask = (0xFu << 4) | (1u << 13);
constexpr uint32_t kCcmGuestOffset50StubReset = 0u;

bool IsChsccdrDiClockTreeValue(uint32_t field, uint32_t reset_field, bool di0) {
    if (field == reset_field) return true;
    const uint32_t selector = field & kChsccdrDiClkSelMask;
    const uint32_t pre_clk_sel = (field & kChsccdrDiPreClkSelMask) >> 6;
    if (di0 && field == (((kChsccdrReset & ~kImx6ChsccdrDi0ClkSelMask) |
                          kImx6ChsccdrDi0ClkSelLdbDi0) & kChsccdrDiFieldMask)) return true;
    return selector == 0u &&
           (pre_clk_sel == kChsccdrPll5PreClkSel || pre_clk_sel == kChsccdrPll3Pfd1PreClkSel);
}

bool IsSupportedChsccdr(uint32_t value) {
    if ((value & ~0x3FFFFu) != 0u) return false;
    const uint32_t di0 = value & kChsccdrDiFieldMask;
    const uint32_t di1 = (value >> 9) & kChsccdrDiFieldMask;
    return IsChsccdrDiClockTreeValue(di0, kChsccdrReset & kChsccdrDiFieldMask, true) &&
           IsChsccdrDiClockTreeValue(di1, (kChsccdrReset >> 9) & kChsccdrDiFieldMask, false);
}

constexpr uint32_t kCbcMrGpu3dShaderPodf = 0xE0000000u;
constexpr uint32_t kCbcMrPrePeriph2Sel = 0x00600000u;
constexpr uint32_t kCbcMrPeriph2Clk2Sel = 0x00100000u;
constexpr uint32_t kCbcMrVpuAxiSel = 0x0000C000u;
constexpr uint32_t kCbcMrPeriphClk2Sel = 0x00003000u;
constexpr uint32_t kCbcMrVdoAxiSel = 0x00000800u;
constexpr uint32_t kCbcMrPcieAxiSel = 0x00000400u;
constexpr uint32_t kCbcMrGpu3dShaderSel = 0x00000300u;
constexpr uint32_t kCbcMrMlbSysSel = 0x00030000u;
constexpr uint32_t kCbcMrMlbSysPodf = 0x03800000u;
constexpr uint32_t kCbcMrReservedHigh = 0x000000C0u;
constexpr uint32_t kCbcMrReservedLow = 0x0000000Cu;
constexpr uint32_t kCbcMrGpu3dAxiSel = 0x00000002u;
constexpr uint32_t kCbcMrGpu2dAxiSel = 0x00000001u;
constexpr uint32_t kCbcMrGuestWritten = kCbcMrPrePeriph2Sel | kCbcMrPeriph2Clk2Sel | kCbcMrPeriphClk2Sel |
                                        kCbcMrGpu3dShaderSel | kCbcMrGpu3dAxiSel | kCbcMrGpu2dAxiSel;

}

bool Imx6Ccm::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && SocId::IsImx6(bd->GetSocId());
}

void Imx6Ccm::OnReady() {
    anatop_ = &emu_.Get<Imx6Anatop>();
    anatop_->RegisterRateListener([this] { ApplyArmClock(); NotifyRates(); });
    Reset();
    ApplyArmClock();
    emu_.Get<PeripheralDispatcher>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        Reset();
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        PostRestore();
    });
}

uint8_t Imx6Ccm::ReadByte(uint32_t addr) {
    return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
}

uint16_t Imx6Ccm::ReadHalf(uint32_t addr) {
    return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
}

uint32_t Imx6Ccm::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (IsRegister(off)) return regs_[off >> 2];
    HaltUnsupportedAccess("read32", addr, 0);
}

void Imx6Ccm::WriteByte(uint32_t addr, uint8_t value) {
    Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte, [this](const Imx6MmioLane& lane) { WriteLane(lane); });
}

void Imx6Ccm::WriteHalf(uint32_t addr, uint16_t value) {
    Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half, [this](const Imx6MmioLane& lane) { WriteLane(lane); });
}

void Imx6Ccm::WriteWord(uint32_t addr, uint32_t value) {
    WriteLane({addr & ~3u, value, 0xFFFFFFFFu});
}

void Imx6Ccm::SaveState(StateWriter& w) { w.WriteBytes("regs", regs_, sizeof(regs_)); }

void Imx6Ccm::RestoreState(StateReader& r) { r.ReadBytes("regs", regs_, sizeof(regs_)); }

void Imx6Ccm::PostRestore() {
    ApplyArmClock();
    NotifyGates();
    NotifyRates();
}

void Imx6Ccm::Reset() {
    ResetRegisters();
}

bool Imx6Ccm::ClockRunsIn(uint32_t ccgr, uint32_t index, FreescaleLowPowerMode mode) const {
    const uint32_t reg = regs_[(kCcgr0 >> 2) + ccgr];
    const uint32_t cg = (reg >> (index * 2u)) & 3u;
    if (cg == 2u) {
        emu_.Get<Fatal>().Die("Imx6Ccm: CCGR%u value 0x%08X CG%u field value %u is reserved",
                              ccgr, reg, index, cg);
    }
    switch (mode) {
        case FreescaleLowPowerMode::kRun:  return cg != 0u;
        case FreescaleLowPowerMode::kWait: return cg == 3u;
        default:                           return false;
    }
}

uint32_t Imx6Ccm::WfiLowPowerMode() const { return regs_[kClpcr >> 2] & kClpcrLpm; }

uint64_t Imx6Ccm::PeriphHz() const {
    const uint32_t cbcdr = regs_[kCbcdr >> 2];
    const uint32_t cbcmr = regs_[kCbcmr >> 2];
    if (((cbcdr >> 25) & 1u) == 0u) {
        switch ((cbcmr >> 18) & 3u) {
            case 0u: return anatop_->Pll2Hz();
            case 1u: return anatop_->Pll2PfdHz(2u);
            case 2u: return anatop_->Pll2PfdHz(0u);
            default: return anatop_->Pll2PfdHz(2u) / 2u;
        }
    }
    uint64_t source_hz = 0u;
    const uint32_t selector = (cbcmr >> 12) & 3u;
    switch (selector) {
        case 0u: source_hz = anatop_->Pll3Hz(); break;
        case 1u: source_hz = 24000000u; break;
        case 2u:
            source_hz = emu_.Get<Imx6Variant>().PeriphClk2UsesPll2Bypass()
                            ? anatop_->Pll2BypassHz() : anatop_->Pll2Hz();
            break;
        default:
            emu_.Get<Fatal>().Die("Imx6Ccm: CBCMR value 0x%08X periph_clk2_sel value %u is reserved",
                                  cbcmr, selector);
    }
    return source_hz / (((cbcdr >> 27) & 7u) + 1u);
}

uint64_t Imx6Ccm::MmdcCh0Hz() const { return PeriphHz() / (((regs_[kCbcdr >> 2] >> 19) & 7u) + 1u); }

GuestCycleClock::Rate Imx6Ccm::UartClockRootRate() const {
    return GuestCycleClock::Rate{anatop_->Pll3Hz() / 6u / ((regs_[kCscdr1 >> 2] & 0x3Fu) + 1u), 1u};
}

uint64_t Imx6Ccm::AxiHz() const {
    const uint32_t cbcdr = regs_[kCbcdr >> 2];
    return PeriphHz() / (((cbcdr >> 16) & 7u) + 1u);
}

uint64_t Imx6Ccm::GpuCoreHz(bool gpu3d) const {
    const uint32_t cbcmr = regs_[kCbcmr >> 2];
    uint64_t hz = 0u;
    const Imx6Variant& variant = emu_.Get<Imx6Variant>();
    const uint32_t selector_shift = variant.GpuSelectorShift(gpu3d);
    const uint32_t divider_shift = variant.GpuDividerShift(gpu3d);
    const uint32_t selector = (cbcmr >> selector_shift) & 3u;
    if (variant.Gpu2dUsesSdlTree() && !gpu3d) {
        switch (selector) {
        case 0u: hz = MmdcCh0Hz(); break;
        case 1u: hz = anatop_->Pll3Hz(); break;
        case 2u: hz = anatop_->Pll2PfdHz(1u); break;
        default: hz = anatop_->Pll3PfdHz(0u); break;
        }
    } else {
        switch (selector) {
        case 0u: hz = gpu3d ? MmdcCh0Hz() : AxiHz(); break;
        case 1u: hz = anatop_->Pll3Hz(); break;
        case 2u: hz = anatop_->Pll2PfdHz(gpu3d ? 1u : 0u); break;
        default: hz = anatop_->Pll2PfdHz(2u); break;
        }
    }
    return hz / (((cbcmr >> divider_shift) & 7u) + 1u);
}

uint64_t Imx6Ccm::IpuHspHz() const {
    const uint32_t cscdr3 = regs_[kCscdr3 >> 2];
    uint64_t hz = 0u;
    switch ((cscdr3 >> 9) & 3u) {
        case 0u: hz = MmdcCh0Hz(); break;
        case 1u: hz = anatop_->Pll2PfdHz(2u); break;
        case 2u: hz = anatop_->Pll3Hz() / 4u; break;
        default: hz = anatop_->Pll3PfdHz(1u); break;
    }
    return hz / (((cscdr3 >> 11) & 7u) + 1u);
}

uint64_t Imx6Ccm::IpuDiHz(uint32_t di) const {
    const uint32_t chsccdr = regs_[kChsccdr >> 2];
    if (!IsSupportedChsccdr(chsccdr))
        emu_.Get<Fatal>().Die("Imx6Ccm: CHSCCDR value 0x%08X has no clock-tree model", chsccdr);
    const uint32_t field = (chsccdr >> (di * 9u)) & 0x1FFu;
    const uint32_t clk_sel = field & 7u;
    const uint32_t podf = (field & kChsccdrDiPodfMask) >> 3;
    if (clk_sel == 0u) {
        const uint32_t pre_clk_sel = (field & kChsccdrDiPreClkSelMask) >> 6;
        if (pre_clk_sel == kChsccdrPll5PreClkSel) return anatop_->Pll5Hz() / (podf + 1u);
        return anatop_->Pll3PfdHz(1u) / (podf + 1u);
    }
    return anatop_->Pll5Hz() / 7u;
}

void Imx6Ccm::RegisterGateListener(std::function<void()> fn) {
    gate_listeners_.Add(std::move(fn));
}

void Imx6Ccm::RegisterRateListener(std::function<void()> fn) {
    rate_listeners_.Add(std::move(fn));
}

bool Imx6Ccm::IsRegister(uint32_t off) {
    if ((off & 3u) != 0u) return false;
    if (off == kCcr || off == kCcmGuestOffset50Stub || off == kClpcr) return true;
    if (off >= 0x10u && off <= 0x3Cu) return true;
    return off >= kCcgr0 && off <= kCcgr6;
}

bool Imx6Ccm::IsWritableRegister(uint32_t off) {
    switch (off) {
    case kCcr:
    case 0x14u:
    case kCacrr:
    case kCbcmr:
    case kCcmGuestOffset50Stub:
    case 0x34u:
    case kClpcr:
    case 0x6Cu:
    case 0x70u:
    case 0x74u:
    case 0x78u:
    case 0x7Cu:
    case 0x80u:
        return true;
    default:
        return false;
    }
}

void Imx6Ccm::ResetRegisters() {
    std::fill(std::begin(regs_), std::end(regs_), 0u);
    regs_[kCcr >> 2] = 0x040116FFu;
    regs_[0x14u >> 2] = 0x00018D00u;
    regs_[0x18u >> 2] = emu_.Get<Imx6Variant>().CcmCbcmrReset();
    regs_[0x1Cu >> 2] = 0x00F00000u;
    regs_[0x20u >> 2] = 0x02B92F06u;
    regs_[0x24u >> 2] = 0x00490B00u;
    regs_[0x28u >> 2] = 0x0EC102C1u;
    regs_[0x2Cu >> 2] = 0x000736C1u;
    regs_[0x30u >> 2] = 0x33F71F92u;
    regs_[kChsccdr >> 2] = kChsccdrReset;
    regs_[0x38u >> 2] = emu_.Get<Imx6Variant>().CcmCscdr2Reset();
    regs_[0x3Cu >> 2] = 0x00010841u;
    regs_[kCcmGuestOffset50Stub >> 2] = kCcmGuestOffset50StubReset;
    regs_[kClpcr >> 2] = kClpcrReset;
    regs_[0x68u >> 2] = 0xFFFFFFFFu;
    regs_[0x6Cu >> 2] = 0xFFFFFFFFu;
    regs_[0x70u >> 2] = 0xFC3FFFFFu;
    regs_[0x74u >> 2] = 0xFFFFFFFFu;
    regs_[0x78u >> 2] = 0xFFFFFFFFu;
    regs_[0x7Cu >> 2] = 0xFFFFFFFFu;
    regs_[0x80u >> 2] = 0xFFFFFFFFu;
    if (emu_.TryGet<Imx6DisplayClockInput>() != nullptr) {
        regs_[kCs2cdr >> 2] &= ~kImx6Cs2cdrLdbDi0SelMask;
        regs_[kCscmr2 >> 2] |= kImx6Cscmr2LdbDi0IpuDiv7;
        regs_[kChsccdr >> 2] = (kChsccdrReset & ~kImx6ChsccdrDi0ClkSelMask) |
                                 kImx6ChsccdrDi0ClkSelLdbDi0;
    }
}

void Imx6Ccm::WriteLane(const Imx6MmioLane& lane) {
    const uint32_t off = lane.address - MmioBase();
    if (off == kCs2cdr && (lane.mask & kImx6Cs2cdrLdbDi0SelMask) != 0u)
        HaltUnsupportedAccess("CS2CDR ldb_di0_clk_sel has no guest writer", lane.address, lane.value);
    if (off == kCs2cdr && (lane.mask & kCs2cdrLdbDi1SelMask) != 0u)
        HaltUnsupportedAccess("CS2CDR ldb_di1_clk_sel has no guest writer", lane.address, lane.value);
    if (off == kCscmr2 && (lane.mask & kImx6Cscmr2LdbDi0IpuDiv7) != 0u)
        HaltUnsupportedAccess("CSCMR2 ldb_di0_ipu_div has no guest writer", lane.address, lane.value);
    if (off == kCscmr2 && (lane.mask & kCscmr2LdbDi1IpuDivMask) != 0u)
        HaltUnsupportedAccess("CSCMR2 ldb_di1_ipu_div has no guest writer", lane.address, lane.value);
    if (!IsWritableRegister(off)) HaltUnsupportedAccess("write lane", lane.address, lane.value);
    const uint32_t old = regs_[off >> 2];
    const uint32_t next = lane.Merge(old);
    if (off == kChsccdr && !IsSupportedChsccdr(next))
        HaltUnsupportedAccess("CHSCCDR value has no clock-tree model", lane.address, lane.value);
    if (off == kCcr) {
        const uint32_t changed = old ^ next;
        if ((changed & 0xF0000000u) != 0u)
            HaltUnsupportedAccess("CCR reserved [31:28] field is read-only", lane.address, lane.value);
        if ((changed & (3u << 19)) != 0u)
            HaltUnsupportedAccess("CCR reserved [20:19] field is read-only", lane.address, lane.value);
        if ((changed & (7u << 13)) != 0u)
            HaltUnsupportedAccess("CCR reserved [15:13] field is read-only", lane.address, lane.value);
        if ((changed & kCcrCoscEn) != 0u)
            HaltUnsupportedAccess("CCR COSC_EN field has no guest writer", lane.address, lane.value);
        if ((changed & (0xFu << 8)) != 0u)
            HaltUnsupportedAccess("CCR reserved [11:8] field is read-only", lane.address, lane.value);
        if ((changed & kCcrOscnt) != 0u)
            HaltUnsupportedAccess("CCR OSCNT field has no guest writer", lane.address, lane.value);
        regs_[kCcr >> 2] = (old & ~kCcrGuestWritten) | (next & kCcrGuestWritten);
        return;
    }
    if (off == kCcmGuestOffset50Stub) {
        const uint32_t changed = old ^ next;
        const uint32_t unsupported = changed & ~kCcmGuestOffset50StoredMask;
        if ((unsupported & 0x0000000Fu) != 0u)
            HaltUnsupportedAccess("CCM+0x50 bits [3:0] have no guest writer", lane.address, lane.value);
        if ((unsupported & 0x00001F00u) != 0u)
            HaltUnsupportedAccess("CCM+0x50 bits [12:8] have no guest writer", lane.address, lane.value);
        if ((unsupported & 0xFFFFC000u) != 0u)
            HaltUnsupportedAccess("CCM+0x50 bits [31:14] have no guest writer", lane.address, lane.value);
        regs_[off >> 2] = (old & ~kCcmGuestOffset50StoredMask) | (next & kCcmGuestOffset50StoredMask);
        return;
    }
    if (off == kClpcr) {
        const uint32_t changed = old ^ next;
        const uint32_t unsupported = changed & ~kClpcrStoredMask;
        if ((unsupported & (1u << 31 | 1u << 30 | 1u << 29 | 1u << 28)) != 0u)
            HaltUnsupportedAccess("CLPCR reserved [31:28] field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 27)) != 0u)
            HaltUnsupportedAccess("CLPCR mask_l2cc_idle field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 26)) != 0u)
            HaltUnsupportedAccess("CLPCR mask_scu_idle field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 25)) != 0u)
            HaltUnsupportedAccess("CLPCR mask_core3_wfi field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 24)) != 0u)
            HaltUnsupportedAccess("CLPCR mask_core2_wfi field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 23)) != 0u)
            HaltUnsupportedAccess("CLPCR mask_core1_wfi field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 22)) != 0u)
            HaltUnsupportedAccess("CLPCR mask_core0_wfi field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 20)) != 0u)
            HaltUnsupportedAccess("CLPCR reserved bit 20 has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 19)) != 0u)
            HaltUnsupportedAccess("CLPCR bypass_mmdc_ch0_lpm_hs field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 18)) != 0u)
            HaltUnsupportedAccess("CLPCR reserved bit 18 has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 17)) != 0u)
            HaltUnsupportedAccess("CLPCR reserved bit 17 has no guest writer", lane.address, lane.value);
        if ((unsupported & (0xFu << 12)) != 0u)
            HaltUnsupportedAccess("CLPCR reserved [15:12] field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 11)) != 0u)
            HaltUnsupportedAccess("CLPCR cosc_pwrdown field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 7)) != 0u)
            HaltUnsupportedAccess("CLPCR dis_ref_osc field has no guest writer", lane.address, lane.value);
        if ((unsupported & (7u << 2)) != 0u)
            HaltUnsupportedAccess("CLPCR reserved [4:2] field has no guest writer", lane.address, lane.value);
        regs_[off >> 2] = (old & ~kClpcrStoredMask) | (next & kClpcrStoredMask);
        return;
    }
    if (off == kCacrr && ((old ^ next) & ~kCacrrArmPodf) != 0u)
        HaltUnsupportedAccess("CACRR reserved field has no modelled effect", lane.address, lane.value);
    if (off == kCbcdr) {
        const uint32_t changed = old ^ next;
        const uint32_t modeled = (7u << 27) | (1u << 25) | (7u << 19) | (7u << 16);
        const uint32_t unsupported = changed & ~modeled;
        if ((unsupported & (3u << 30)) != 0u)
            HaltUnsupportedAccess("CBCDR reserved [31:30] field has no guest writer", lane.address, lane.value);
        if ((unsupported & (1u << 26)) != 0u)
            HaltUnsupportedAccess("CBCDR periph2_clk_sel [26] field has no modelled effect", lane.address, lane.value);
        if ((unsupported & (7u << 22)) != 0u)
            HaltUnsupportedAccess("CBCDR reserved [24:22] field has no guest writer", lane.address, lane.value);
        if ((unsupported & (7u << 13)) != 0u)
            HaltUnsupportedAccess("CBCDR reserved [15:13] field has no guest writer", lane.address, lane.value);
        if ((unsupported & (7u << 10)) != 0u)
            HaltUnsupportedAccess("CBCDR ahb_podf field has no modelled effect", lane.address, lane.value);
        if ((unsupported & (3u << 8)) != 0u)
            HaltUnsupportedAccess("CBCDR ipg_podf field has no modelled effect", lane.address, lane.value);
        if ((unsupported & (1u << 7)) != 0u)
            HaltUnsupportedAccess("CBCDR axi_alt_sel field has no modelled effect", lane.address, lane.value);
        if ((unsupported & (1u << 6)) != 0u)
            HaltUnsupportedAccess("CBCDR axi_sel field has no modelled effect", lane.address, lane.value);
        if ((unsupported & (7u << 3)) != 0u)
            HaltUnsupportedAccess("CBCDR mmdc_ch1_axi_podf [5:3] field has no modelled effect", lane.address, lane.value);
        if ((unsupported & 7u) != 0u)
            HaltUnsupportedAccess("CBCDR periph2_clk2_podf [2:0] field has no modelled effect", lane.address, lane.value);
    }
    if (off == kCbcmr) {
        const uint32_t changed = old ^ next;
        const Imx6Variant& variant = emu_.Get<Imx6Variant>();
        const uint32_t consumed = variant.CbcmrConsumedMask();
        const uint32_t unsupported = changed & ~(consumed | kCbcMrGuestWritten);
        if (variant.HasGpu3dShaderPodf() && (unsupported & kCbcMrGpu3dShaderPodf) != 0u)
            HaltUnsupportedAccess("CBCMR gpu3d_shader_podf field has no guest writer", lane.address, lane.value);
        if (variant.HasMlbSysPodf() && (unsupported & kCbcMrMlbSysPodf) != 0u)
            HaltUnsupportedAccess("CBCMR mlb_sys_clk_podf field has no guest writer", lane.address, lane.value);
        if (variant.HasMlbSysPodf() && (unsupported & kCbcMrMlbSysSel) != 0u)
            HaltUnsupportedAccess("CBCMR mlb_sys_sel field has no guest writer", lane.address, lane.value);
        if ((unsupported & kCbcMrVpuAxiSel) != 0u)
            HaltUnsupportedAccess("CBCMR vpu_axi_clk_sel field has no guest writer", lane.address, lane.value);
        if ((unsupported & kCbcMrVdoAxiSel) != 0u)
            HaltUnsupportedAccess("CBCMR vdoaxi_clk_sel field has no guest writer", lane.address, lane.value);
        if ((unsupported & kCbcMrPcieAxiSel) != 0u)
            HaltUnsupportedAccess("CBCMR pcie_axi_clk_sel field has no guest writer", lane.address, lane.value);
        if ((unsupported & kCbcMrReservedHigh) != 0u)
            HaltUnsupportedAccess("CBCMR reserved [7:6] field has no guest writer", lane.address, lane.value);
        if ((unsupported & kCbcMrReservedLow) != 0u)
            HaltUnsupportedAccess("CBCMR reserved [3:2] field has no guest writer", lane.address, lane.value);
    }
    regs_[off >> 2] = lane.Merge(old);
    if (off == kCacrr && regs_[off >> 2] != old) {
        ApplyArmClock();
        NotifyRates();
    }
    if (off >= kCcgr0 && off <= kCcgr6 && regs_[off >> 2] != old) NotifyGates();
    if ((off == kCbcmr || off == kCbcdr || off == kChsccdr) && regs_[off >> 2] != old) NotifyRates();
}

void Imx6Ccm::NotifyGates() {
    gate_listeners_.Call();
}

void Imx6Ccm::NotifyRates() {
    rate_listeners_.Call();
}

void Imx6Ccm::ApplyArmClock() {
    emu_.Get<GuestCycleClock>().SetClockHz(ArmClockHz());
}

uint64_t Imx6Ccm::ArmClockHz() const {
    return anatop_->PllArmHz() / ((regs_[kCacrr >> 2] & kCacrrArmPodf) + 1u);
}

REGISTER_SERVICE(Imx6Ccm);

}
