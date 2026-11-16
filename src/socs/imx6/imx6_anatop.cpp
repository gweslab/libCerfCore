#include "imx6_anatop.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_boot_display_clock.h"
#include "imx6_display_clock_input.h"
#include "imx6_id.h"
#include "imx6_variant.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kImx6SlDigprogAbsentStub = 0u;
constexpr uint32_t kPllPower = 0x00001000u;
constexpr uint32_t kPmuRegCore = 0x140u;
constexpr uint32_t kPmuRegCoreReset = 0x00402010u;

constexpr uint32_t kPllBypass = 1u << 16;
constexpr uint32_t kPllUsb2 = 0x020u;
constexpr uint32_t kPllEnet = 0x0E0u;
constexpr uint32_t kUsb1ChrgDetect = 0x1B0u;
constexpr uint32_t kUsb2VbusDetect = 0x200u;
constexpr uint32_t kUsb2ChrgDetect = 0x210u;
constexpr uint32_t kPllUsb1 = 0x010u;
constexpr uint32_t kPllSys = 0x030u;
constexpr uint32_t kPfd480 = 0x0F0u;
constexpr uint32_t kPfd528 = 0x100u;
constexpr uint32_t kPllVideo = 0x0A0u;
constexpr uint32_t kPllVideoNum = 0x0B0u;
constexpr uint32_t kPllVideoDenom = 0x0C0u;
constexpr uint32_t kMisc2 = 0x170u;
constexpr uint32_t kMisc2VideoDivShift = 30u;
constexpr uint32_t kPllAudioWritable = 0x0019307Fu;
constexpr uint32_t kPllArmWritable = 0x0001307Fu;
constexpr uint32_t kPllSysWritable = 0x00000001u;
constexpr uint32_t kPllUsb1Writable = kPllSysWritable | (1u << 6);
constexpr uint32_t kPllUsb2Writable = kPllBypass | (3u << 14) | kImx6PllEnable | kPllPower | (1u << 6) | 3u;
constexpr uint32_t kPllEnetWritable = kPllBypass | kImx6PllEnable | kPllPower | 3u;
constexpr uint32_t kUsb1ChrgDetectWritable = (1u << 20) | (1u << 19);
constexpr uint32_t kUsb2ChrgDetectWritable = (1u << 20) | (1u << 19);
constexpr uint32_t kPllVideoWritable = 0x0019307Fu;
constexpr uint32_t kVideoFractionWritable = 0x3FFFFFFFu;
constexpr uint32_t kPfd480Writable = 0xBFBFBFBFu;
constexpr uint32_t kPfd528Writable = 0x00BFBFBFu;
constexpr uint32_t kMisc2Writable = (1u << 23) | (1u << 15) | (3u << kMisc2VideoDivShift);

}

bool Imx6Anatop::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && SocId::IsImx6(bd->GetSocId());
}

void Imx6Anatop::OnReady() {
    Reset();
    emu_.Get<PeripheralDispatcher>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        Reset();
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        NotifyRates();
    });
}

uint8_t Imx6Anatop::ReadByte(uint32_t addr) {
    if (((addr - MmioBase()) & ~3u) == 0x280u)
        HaltUnsupportedAccess("read8 i.MX6SL DIGPROG probe", addr, 0);
    return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
}

uint16_t Imx6Anatop::ReadHalf(uint32_t addr) {
    if (((addr - MmioBase()) & ~3u) == 0x280u)
        HaltUnsupportedAccess("read16 i.MX6SL DIGPROG probe", addr, 0);
    return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
}

uint32_t Imx6Anatop::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off == 0x260u)
        return emu_.Get<Imx6Variant>().AnatopDigprog();
    if (off == 0x280u) return kImx6SlDigprogAbsentStub;
    if (IsRegister(off)) return regs_[(off & ~0xFu) / 0x10u];
    HaltUnsupportedAccess("read32", addr, 0);
}

void Imx6Anatop::WriteByte(uint32_t addr, uint8_t value) {
    Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte, [this](const Imx6MmioLane& lane) { WriteLane(lane); });
}

void Imx6Anatop::WriteHalf(uint32_t addr, uint16_t value) {
    Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half, [this](const Imx6MmioLane& lane) { WriteLane(lane); });
}

void Imx6Anatop::WriteWord(uint32_t addr, uint32_t value) { WriteLane({addr, value, 0xFFFFFFFFu}); }

void Imx6Anatop::SaveState(StateWriter& w) { w.WriteBytes("regs", regs_, sizeof(regs_)); }

void Imx6Anatop::RestoreState(StateReader& r) { r.ReadBytes("regs", regs_, sizeof(regs_)); }

void Imx6Anatop::PostRestore() { NotifyRates(); }

uint64_t Imx6Anatop::Pll2Hz() const { return PllHz(kPllSys, "PLL2"); }

uint64_t Imx6Anatop::Pll2BypassHz() const {
    if ((regs_[kPllSys / 0x10u] & kPllBypass) != 0u) return kImx6RefHz;
    return Pll2Hz();
}

uint64_t Imx6Anatop::Pll3Hz() const { return PllHz(kPllUsb1, "PLL3"); }

uint64_t Imx6Anatop::PllArmHz() const {
    const uint32_t reg = regs_[0];
    if ((reg & kPllBypass) != 0u) return kImx6RefHz;
    if ((reg & kImx6PllEnable) == 0u || (reg & kPllPower) != 0u)
        emu_.Get<Fatal>().Die("Imx6Anatop: PLL_ARM control 0x%08X is disabled or powered down; "
                              "ARM clock is not available", reg);
    const uint32_t div_select = reg & 0x7Fu;
    if (div_select < 54u || div_select > 108u)
        emu_.Get<Fatal>().Die("Imx6Anatop: PLL_ARM DIV_SELECT %u is outside the documented 54-108 range",
                              div_select);
    return kImx6RefHz * div_select / 2u;
}

uint64_t Imx6Anatop::Pll5Hz() const {
    const uint32_t reg = regs_[kPllVideo / 0x10u];
    if ((reg & kPllBypass) != 0u || (reg & kImx6PllEnable) == 0u || !IsPllPowered(kPllVideo, reg)) {
        emu_.Get<Fatal>().Die("Imx6Anatop: PLL5 control 0x%08X is bypassed, gated or powered down; "
                              "that output is not modelled", reg);
    }
    const uint32_t div_select = reg & 0x7Fu;
    const uint32_t num_field  = regs_[kPllVideoNum / 0x10u] & 0x3FFFFFFFu;
    const int64_t  num        = (num_field & 0x20000000u) != 0u ? static_cast<int64_t>(num_field) - 0x40000000ll
                                                                 : static_cast<int64_t>(num_field);
    const int64_t  denom      = regs_[kPllVideoDenom / 0x10u] & 0x3FFFFFFFu;
    const uint32_t post       = (reg >> 19) & 3u;
    if (div_select < 27u || div_select > 54u || denom == 0 || (num < 0 ? -num : num) >= denom || post == 3u) {
        emu_.Get<Fatal>().Die("Imx6Anatop: PLL5 DIV_SELECT %u, NUM %lld, DENOM %lld, POST_DIV_SELECT %u "
                              "is outside the documented ranges", div_select, static_cast<long long>(num),
                              static_cast<long long>(denom), post);
    }
    const int64_t  vco_hz = static_cast<int64_t>(kImx6RefHz) * (static_cast<int64_t>(div_select) * denom + num) / denom;
    const uint32_t video_div = (regs_[kMisc2 / 0x10u] >> kMisc2VideoDivShift) & 3u;
    const uint64_t divider = (post == 0u ? 4u : post == 1u ? 2u : 1u) *
                             (video_div == 1u ? 2u : video_div == 3u ? 4u : 1u);
    return static_cast<uint64_t>(vco_hz) / divider;
}

uint64_t Imx6Anatop::Pll2PfdHz(uint32_t pfd) const {
    return PfdHz(regs_[kPfd528 / 0x10u], pfd, Pll2Hz(), "PLL2");
}

uint64_t Imx6Anatop::Pll3PfdHz(uint32_t pfd) const {
    return PfdHz(regs_[kPfd480 / 0x10u], pfd, Pll3Hz(), "PLL3");
}

void Imx6Anatop::RegisterRateListener(std::function<void()> fn) {
    rate_listeners_.Add(std::move(fn));
}

bool Imx6Anatop::IsPllControl(uint32_t base) {
    switch (base) {
    case 0x000u: case 0x010u: case 0x020u: case 0x030u:
    case 0x070u: case 0x0A0u: case 0x0D0u: case 0x0E0u:
        return true;
    default:
        return false;
    }
}

bool Imx6Anatop::IsPllPowered(uint32_t base, uint32_t reg) {
    switch (base) {
    case 0x010u:
        return (reg & kPllPower) != 0u;
    case kPllUsb2:
        return (reg & kPllPower) != 0u;
    case kPllEnet: case 0x000u: case 0x030u: case 0x070u: case 0x0A0u:
        return (reg & kPllPower) == 0u;
    default:
        return false;
    }
}

void Imx6Anatop::UpdatePllLockAfterWrite(uint32_t base, uint32_t& reg) {
    if (!IsPllControl(base)) return;
    reg &= ~kImx6PllLock;
    if (IsPllPowered(base, reg)) reg |= kImx6PllLock;
}

bool Imx6Anatop::HasAliases(uint32_t base) {
    switch (base) {
    case 0x000u: case 0x010u: case 0x020u: case 0x030u: case kPmuRegCore:
    case 0x070u: case 0x0A0u: case 0x0D0u: case 0x0E0u:
    case 0x0F0u: case 0x100u: case 0x150u: case 0x160u:
    case 0x170u: case 0x1A0u: case 0x1B0u: case 0x1F0u:
    case 0x200u: case 0x210u: case 0x250u:
        return true;
    default:
        return false;
    }
}

bool Imx6Anatop::IsSingleRegister(uint32_t off) {
    switch (off) {
    case 0x040u: case 0x050u: case 0x060u: case 0x080u:
    case 0x090u: case 0x0B0u: case 0x0C0u: case 0x1C0u:
    case 0x1D0u: case 0x220u: case 0x230u: case 0x260u:
        return true;
    default:
        return false;
    }
}

bool Imx6Anatop::IsRegister(uint32_t off) {
    if ((off & 3u) != 0u) return false;
    return HasAliases(off & ~0xFu) || IsSingleRegister(off);
}

bool Imx6Anatop::IsWritableRegister(uint32_t off) {
    if (!IsRegister(off)) return false;
    return off != 0x1C0u && off != 0x1D0u && off != 0x220u && off != 0x230u && off != 0x260u;
}

void Imx6Anatop::Reset() {
    std::fill(std::begin(regs_), std::end(regs_), 0u);
    regs_[kPllUsb2 / 0x10u] = 0x00012000u;
    regs_[0x060u / 0x10u] = 0x00000012u;
    regs_[0x070u / 0x10u] = 0x00011006u;
    regs_[0x080u / 0x10u] = 0x05F5E100u;
    regs_[0x090u / 0x10u] = 0x2964619Cu;
    regs_[0x0A0u / 0x10u] = 0x0001100Cu;
    regs_[0x0B0u / 0x10u] = 0x05F5E100u;
    regs_[0x0C0u / 0x10u] = 0x10A24447u;
    regs_[0x0D0u / 0x10u] = 0x00010000u;
    regs_[0x0E0u / 0x10u] = 0x00011001u;
    regs_[0x0F0u / 0x10u] = 0x1311100Cu;
    regs_[0x100u / 0x10u] = 0x1018101Bu;
    regs_[kPmuRegCore / 0x10u] = kPmuRegCoreReset;
    regs_[0x150u / 0x10u] = 0x04000000u;
    regs_[0x170u / 0x10u] = 0x00272727u;
    regs_[0x1A0u / 0x10u] = 0x00100004u;
    regs_[0x1F0u / 0x10u] = 0x00000002u;
    regs_[0x200u / 0x10u] = 0x00100004u;
    regs_[0x250u / 0x10u] = 0x00000002u;
    regs_[0x000u / 0x10u] = 0x80002042u;
    regs_[0x010u / 0x10u] = 0x80003000u;
    regs_[0x030u / 0x10u] = 0x80002001u;
    if (const auto* display = emu_.TryGet<Imx6DisplayClockInput>()) {
        Imx6BootDisplayClock boot{};
        if (!EncodeImx6BootDisplayClock(display->PixelClockHz(), boot)) {
            emu_.Get<Fatal>().Die("Imx6Anatop: no PLL5 setting gives the LDB serial clock of a %llu Hz "
                                  "pixel clock", static_cast<unsigned long long>(display->PixelClockHz()));
        }
        regs_[kPllVideo / 0x10u]      = boot.pll_video;
        regs_[kPllVideoNum / 0x10u]   = boot.pll_video_num;
        regs_[kPllVideoDenom / 0x10u] = boot.pll_video_denom;
        regs_[kMisc2 / 0x10u] = (regs_[kMisc2 / 0x10u] & ~(3u << kMisc2VideoDivShift)) |
                                (boot.misc2_video_div << kMisc2VideoDivShift);
    }
}

void Imx6Anatop::WriteLane(const Imx6MmioLane& lane) {
    const uint32_t off = lane.address - MmioBase();
    if ((off & ~0xCu) == kPmuRegCore)
        HaltUnsupportedAccess("write to PMU_REG_CORE", lane.address, lane.value);
    if (!IsWritableRegister(off)) HaltUnsupportedAccess("write lane", lane.address, lane.value);
    Store(off & ~0xFu, off & 0xCu, lane.mask, lane.value);
}

void Imx6Anatop::Store(uint32_t base, uint32_t alias, uint32_t mask, uint32_t value) {
    uint32_t&      reg         = regs_[base / 0x10u];
    const uint32_t old         = reg;
    uint32_t       modeled     = 0u;
    const char*    name        = "unmodeled ANATOP register";
    switch (base) {
    case 0x000u: modeled = kPllArmWritable; name = "PLL_ARM"; break;
    case kPllUsb1: modeled = kPllUsb1Writable; name = "PLL_USB1"; break;
    case kPllSys: modeled = kPllSysWritable; name = "PLL_SYS"; break;
    case kPllVideo: modeled = kPllVideoWritable; name = "PLL_VIDEO"; break;
    case kPfd480: modeled = kPfd480Writable; name = "PFD_480"; break;
    case kPfd528: modeled = kPfd528Writable; name = "PFD_528"; break;
    case kPllVideoNum: modeled = kVideoFractionWritable; name = "PLL_VIDEO_NUM"; break;
    case kPllVideoDenom: modeled = kVideoFractionWritable; name = "PLL_VIDEO_DENOM"; break;
    case kMisc2: modeled = kMisc2Writable; name = "MISC2"; break;
    case kPllUsb2: modeled = kPllUsb2Writable; name = "PLL_USB2"; break;
    case 0x070u: modeled = kPllAudioWritable; name = "PLL_AUDIO"; break;
    case 0x080u: modeled = kVideoFractionWritable; name = "PLL_AUDIO_NUM"; break;
    case 0x090u: modeled = kVideoFractionWritable; name = "PLL_AUDIO_DENOM"; break;
    case 0x0D0u: name = "PLL_MLB"; break;
    case kPllEnet: modeled = kPllEnetWritable; name = "PLL_ENET"; break;
    case 0x1A0u: name = "USB1_VBUS_DETECT"; break;
    case kUsb1ChrgDetect: modeled = kUsb1ChrgDetectWritable; name = "USB1_CHRG_DETECT"; break;
    case kUsb2VbusDetect: modeled = (1u << 20); name = "USB2_VBUS_DETECT"; break;
    case kUsb2ChrgDetect: modeled = kUsb2ChrgDetectWritable; name = "USB2_CHRG_DETECT"; break;
    case 0x150u: name = "MISC0"; break;
    case 0x160u: name = "MISC1"; break;
    default: break;
    }
    if (IsPllControl(base)) modeled |= kImx6PllLock;
    const uint32_t write_mask  = mask & (IsPllControl(base) ? ~kImx6PllLock : 0xFFFFFFFFu);
    const uint32_t write_value = value & write_mask;
    uint32_t next = old;
    switch (alias) {
    case 0x0: next = (old & ~write_mask) | write_value; break;
    case 0x4: next = old | write_value; break;
    case 0x8: next = old & ~write_value; break;
    case 0xC: next = old ^ write_value; break;
    }
    const uint32_t changed = old ^ next;
    if (base == kPfd480 || base == kPfd528) {
        const uint32_t frac_mask = base == kPfd480 ? 0x3F3F3F3Fu : 0x003F3F3Fu;
        const uint32_t stable_mask = base == kPfd480 ? 0x40404040u : 0x00404040u;
        next = (next & ~stable_mask) | (old & stable_mask);
        const uint32_t frac_changed = (old ^ next) & frac_mask;
        for (uint32_t shift = 0u; shift < (base == kPfd480 ? 32u : 24u); shift += 8u) {
            if ((frac_changed & (0x3Fu << shift)) != 0u)
                next ^= (1u << (shift + 6u));
        }
        modeled |= stable_mask;
    }
    const uint32_t unsupported = changed & ~modeled;
    if (unsupported != 0u)
        emu_.Get<Fatal>().Die("Imx6Anatop: %s at 0x%08X field bits 0x%08X have no modeled effect",
                              name, MmioBase() + base, unsupported);
    reg = (old & ~modeled) | (next & modeled);
    UpdatePllLockAfterWrite(base, reg);
    const bool rate_source = base == 0x000u || base == kPllUsb1 || base == kPllSys || base == kPfd480 || base == kPfd528 ||
                             base == kPllVideo || base == kPllVideoNum || base == kPllVideoDenom ||
                             base == kMisc2 || base == kPllUsb2;
    if (rate_source && reg != old) NotifyRates();
}

uint64_t Imx6Anatop::PllHz(uint32_t base, const char* name) const {
    const uint32_t reg = regs_[base / 0x10u];
    if ((reg & kPllBypass) != 0u || (reg & kImx6PllEnable) == 0u || !IsPllPowered(base, reg)) {
        emu_.Get<Fatal>().Die("Imx6Anatop: %s control 0x%08X is bypassed, gated or powered down; "
                              "that output is not modelled", name, reg);
    }
    const uint32_t div_select = reg & 3u;
    if (div_select > 1u) {
        emu_.Get<Fatal>().Die("Imx6Anatop: %s DIV_SELECT %u has no documented ratio", name,
                              div_select);
    }
    return kImx6RefHz * (div_select != 0u ? 22u : 20u);
}

uint64_t Imx6Anatop::PfdHz(uint32_t reg, uint32_t pfd, uint64_t pll_hz, const char* name) const {
    const uint32_t field = (reg >> (pfd * 8u)) & 0xFFu;
    const uint32_t frac  = field & 0x3Fu;
    if ((field & 0x80u) != 0u || frac < 12u || frac > 35u) {
        emu_.Get<Fatal>().Die("Imx6Anatop: %s PFD%u field 0x%02X is gated or outside 12-35; that "
                              "output is not modelled", name, pfd, field);
    }
    return pll_hz * 18u / frac;
}

void Imx6Anatop::NotifyRates() {
    rate_listeners_.Call();
}

REGISTER_SERVICE(Imx6Anatop);

}
