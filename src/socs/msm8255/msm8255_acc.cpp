#include "msm8255_acc.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <atomic>
#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kAccBase = 0xC0101000u;
constexpr uint32_t kAccSize = 0x00001000u;

constexpr uint32_t kRegClkCtl      = 0x04u;
constexpr uint32_t kRegClkSel      = 0x08u;
constexpr uint32_t kReg10          = 0x10u;
constexpr uint32_t kReg14          = 0x14u;
constexpr uint32_t kRegAppsPwrdown = 0x1Cu;
constexpr uint32_t kReg20          = 0x20u;

constexpr uint32_t kAppsPowerDown  = 1u;
constexpr uint32_t kReg20Bits      = 1u;
constexpr uint32_t kSavedPairValue = 0u;

constexpr uint32_t kClkCtl = 0x00003000u;
constexpr uint32_t kClkSel = 0x00000000u;

constexpr uint32_t kClkSelSrc1No = 0x1u;
constexpr uint32_t kClkCtlBanks  = 0x7F7Fu;
constexpr uint32_t kResetSource  = 3u;
constexpr uint32_t kResetDivider = 0u;

}

bool Msm8255Acc::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
}

void Msm8255Acc::OnReady() {
    clk_ctl_.store(kClkCtl, std::memory_order_release);
    clk_sel_.store(kClkSel, std::memory_order_release);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        clk_ctl_.store(kClkCtl, std::memory_order_release);
        clk_sel_.store(kClkSel, std::memory_order_release);
        apps_pwrdown_.store(0u, std::memory_order_release);
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Msm8255Acc::MmioBase() const { return kAccBase; }
uint32_t Msm8255Acc::MmioSize() const { return kAccSize; }

uint32_t Msm8255Acc::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
    case kRegClkCtl: return clk_ctl_.load(std::memory_order_acquire);
    case kRegClkSel: return clk_sel_.load(std::memory_order_acquire);
    case kReg10:
    case kReg14:     return kSavedPairValue;
    default:         HaltUnsupportedAccess("ReadWord", addr, 0);
    }
}

void Msm8255Acc::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off == kRegClkCtl && (value & ~kClkCtlBanks) == 0u) {
        RequireModeledSource(value, clk_sel_.load(std::memory_order_acquire));
        clk_ctl_.store(value, std::memory_order_release);
        return;
    }
    if (off == kRegClkSel && (value & ~kClkSelSrc1No) == 0u) {
        RequireModeledSource(clk_ctl_.load(std::memory_order_acquire), value);
        clk_sel_.store(value, std::memory_order_release);
        return;
    }
    if (off == kRegAppsPwrdown && (value & ~kAppsPowerDown) == 0u) {
        apps_pwrdown_.store(value, std::memory_order_release);
        return;
    }
    if ((off == kReg20 && (value & ~kReg20Bits) == 0u) ||
        ((off == kReg10 || off == kReg14) && value == kSavedPairValue)) {
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

bool Msm8255Acc::AppsPowerDown() const {
    return (apps_pwrdown_.load(std::memory_order_acquire) & kAppsPowerDown) != 0u;
}

void Msm8255Acc::SaveState(StateWriter& w) {
    w.Write<uint32_t>("acc_clk_ctl", clk_ctl_.load(std::memory_order_acquire));
    w.Write<uint32_t>("acc_clk_sel", clk_sel_.load(std::memory_order_acquire));
    w.Write<uint32_t>("acc_apps_pwrdown", apps_pwrdown_.load(std::memory_order_acquire));
}

void Msm8255Acc::RestoreState(StateReader& r) {
    uint32_t clk_ctl     = kClkCtl;
    uint32_t clk_sel     = kClkSel;
    uint32_t apps_pwrdown = 0u;
    r.Read("acc_clk_ctl", clk_ctl);
    r.Read("acc_clk_sel", clk_sel);
    r.Read("acc_apps_pwrdown", apps_pwrdown);
    clk_ctl_.store(clk_ctl, std::memory_order_release);
    clk_sel_.store(clk_sel, std::memory_order_release);
    apps_pwrdown_.store(apps_pwrdown, std::memory_order_release);
}

void Msm8255Acc::RequireModeledSource(uint32_t clk_ctl, uint32_t clk_sel) {
    const uint32_t bank    = (clk_sel & kClkSelSrc1No) != 0u ? 0u : 8u;
    const uint32_t source  = (clk_ctl >> (bank + 4u)) & 7u;
    const uint32_t divider = (clk_ctl >> bank) & 0xFu;
    if (source != kResetSource || divider != kResetDivider) {
        emu_.Get<Fatal>().Die(
            "msm8255 acc: the cpu clock on source %u divider %u is not modeled", source,
            divider);
    }
}

REGISTER_SERVICE(Msm8255Acc);

}
