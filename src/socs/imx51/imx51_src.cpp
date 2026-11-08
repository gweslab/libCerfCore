#include "imx51_src.h"

#include <cerfcore/boards/board_context.h>
#include "imx51_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t kBase = 0x73FD0000u;
constexpr uint32_t kSize = 0x00004000u;

constexpr uint32_t kScr  = 0x00u;
constexpr uint32_t kSbmr = 0x04u;
constexpr uint32_t kSrsr = 0x08u;
constexpr uint32_t kSisr = 0x14u;
constexpr uint32_t kSimr = 0x18u;

constexpr uint32_t kScrReset             = 0x00000521u;
constexpr uint32_t kScrWarmResetEnable   = 0x00000001u;
constexpr uint32_t kScrSoftwareResets    = 0x0000001Eu;
constexpr uint32_t kScrBypassCount       = 0x00000060u;
constexpr uint32_t kScrMaskWdogShift     = 7u;
constexpr uint32_t kScrMaskWdogField     = 0x00000780u;
constexpr uint32_t kScrWdogMaskedCode    = 0x5u;
constexpr uint32_t kScrWdogNotMaskedCode = 0xAu;
constexpr uint32_t kSimrReset            = 0x00000007u;
constexpr uint32_t kSimrMasks            = 0x0000000Fu;

constexpr uint32_t kSrsrIppReset = 0x00000001u;
constexpr uint32_t kSrsrWdog     = 0x00000010u;
constexpr uint32_t kSrsrWarmBoot = 0x00010000u;
constexpr uint32_t kSrsrWriteOneToClear = 0x0000007Du;

}

REGISTER_SERVICE(Imx51Src);

bool Imx51Src::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx51;
}

void Imx51Src::OnReady() {
    ResetRegisters();
    srsr_.store(kSrsrIppReset, std::memory_order_release);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        if (kind == ResetLineKind::Rtc) ResetRegisters();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
    emu_.Get<GuestCpuReset>().SetCauseLatch(this);
}

void Imx51Src::ResetRegisters() {
    scr_  = kScrReset;
    sisr_ = 0;
    simr_ = kSimrReset;
}

void Imx51Src::LatchColdReset()      { srsr_.store(kSrsrIppReset, std::memory_order_release); }
void Imx51Src::LatchWarmReset()      { srsr_.fetch_or(kSrsrWarmBoot, std::memory_order_acq_rel); }
void Imx51Src::LatchWatchdogReset()  { srsr_.fetch_or(kSrsrWdog, std::memory_order_acq_rel); }
void Imx51Src::ReportOnlyWarmReset() { srsr_.store(kSrsrWarmBoot, std::memory_order_release); }

bool Imx51Src::WarmResetKeepsConfiguration(ResetLineKind kind) const {
    return kind != ResetLineKind::Rtc && (scr_ & kScrWarmResetEnable) != 0u;
}

uint32_t Imx51Src::MmioBase() const { return kBase; }
uint32_t Imx51Src::MmioSize() const { return kSize; }

uint32_t Imx51Src::ReadWord(uint32_t addr) {
    switch (addr - kBase) {
        case kScr:  return scr_;
        case kSrsr: return srsr_.load(std::memory_order_acquire);
        case kSisr: return sisr_;
        case kSimr: return simr_;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Imx51Src::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - kBase) {
        case kScr:  WriteScr(value); return;
        case kSbmr: return;
        case kSrsr: WriteSrsr(value); return;
        case kSisr: sisr_ &= ~value; return;
        case kSimr: simr_ = value & kSimrMasks; return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Imx51Src::WriteScr(uint32_t value) {
    const uint32_t mask_code = (value & kScrMaskWdogField) >> kScrMaskWdogShift;
    if ((value & kScrSoftwareResets) != 0u || (value & kScrWarmResetEnable) == 0u ||
        mask_code == kScrWdogMaskedCode) {
        HaltUnsupportedAccess("WriteWord", kBase + kScr, value);
    }
    scr_ = (kScrWdogNotMaskedCode << kScrMaskWdogShift) | (value & kScrBypassCount) | kScrWarmResetEnable;
}

void Imx51Src::WriteSrsr(uint32_t value) {
    uint32_t old = srsr_.load(std::memory_order_acquire);
    uint32_t next = 0;
    do {
        next = (old & ~(value & kSrsrWriteOneToClear) & ~kSrsrWarmBoot) | (value & kSrsrWarmBoot);
    } while (!srsr_.compare_exchange_weak(old, next, std::memory_order_acq_rel, std::memory_order_acquire));
}

void Imx51Src::SaveState(StateWriter& w) {
    w.Write("scr", scr_);
    w.Write("srsr", srsr_.load(std::memory_order_acquire));
    w.Write("sisr", sisr_);
    w.Write("simr", simr_);
}

void Imx51Src::RestoreState(StateReader& r) {
    r.Read("scr", scr_);
    uint32_t s = 0;
    r.Read("srsr", s);
    srsr_.store(s, std::memory_order_release);
    r.Read("sisr", sisr_);
    r.Read("simr", simr_);
}

}
