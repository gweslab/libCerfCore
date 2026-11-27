#include "adreno_a2xx_gpu.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "adreno_a2xx_command_processor.h"
#include "adreno_a2xx_context.h"
#include "adreno_a2xx_draw.h"
#include "adreno_a2xx_interrupts.h"
#include "adreno_a2xx_memory.h"
#include "adreno_a2xx_pixel_queue.h"
#include "adreno_a2xx_pixel_stage.h"
#include "adreno_a2xx_regs.h"
#include "irq_controller.h"

#include <cstdint>

namespace cerfcore {

using namespace adreno_a2xx_regs;

namespace {

constexpr uint32_t kMmuEnable                 = 1u << 0;
constexpr uint32_t kMmuSplitMode              = 1u << 1;
constexpr uint32_t kMmuBehaviorShift          = 4u;
constexpr uint32_t kMmuBehaviorBits           = 2u;
constexpr uint32_t kMmuBehaviorMask           = 3u;
constexpr uint32_t kMmuClientCount            = 11u;
constexpr uint32_t kMmuBehaviorFields         = 0x03FFFFF0u;
constexpr uint32_t kMmuConfigDefined          = kMmuEnable | kMmuSplitMode | kMmuBehaviorFields;
constexpr uint32_t kMmuBehaviorTranslateRange = 1u;
constexpr uint32_t kMmuInvalidateDefined      = 3u;

}

AdrenoA2xxGpu::AdrenoA2xxGpu(CerfEmulator& emu, uint32_t base, uint32_t size, int irq_line, uint32_t gmem_bytes)
    : Peripheral(emu), base_(base), size_(size), irq_line_(irq_line), gmem_bytes_(gmem_bytes) {}

uint32_t AdrenoA2xxGpu::GmemBytes() const { return gmem_bytes_; }

void AdrenoA2xxGpu::PublishInterruptLine() {
    auto& irq = emu_.Get<IrqController>();
    if (emu_.Get<AdrenoA2xxInterrupts>().Asserted()) irq.AssertIrq(irq_line_);
    else                                             irq.DeAssertIrq(irq_line_);
}

void AdrenoA2xxGpu::RaiseCpInterrupt(uint32_t bits, uint32_t address) {
    emu_.Get<AdrenoA2xxInterrupts>().RaiseCp(bits, address);
    PublishInterruptLine();
}

uint32_t AdrenoA2xxGpu::ReadRegister(uint32_t idx) { return ReadWord(base_ + idx * 4u); }

std::unordered_map<uint32_t, uint32_t>& AdrenoA2xxGpu::RegisterFile() { return reg_file_; }

void AdrenoA2xxGpu::PostRestore() { PublishInterruptLine(); }

void AdrenoA2xxGpu::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { PowerDown(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t AdrenoA2xxGpu::MmioBase() const { return base_; }
uint32_t AdrenoA2xxGpu::MmioSize() const { return size_; }

uint32_t AdrenoA2xxGpu::ReadWord(uint32_t a) {
    const uint32_t idx = (a - base_) >> 2;
    if (auto& interrupts = emu_.Get<AdrenoA2xxInterrupts>(); interrupts.Owns(idx))
        return interrupts.Read(idx);
    if (auto& cp = emu_.Get<AdrenoA2xxCommandProcessor>(); cp.Owns(idx)) return cp.Read(idx);
    switch (idx) {
        case kIdxPmOverride1: return pm_override1_;
        case kIdxPmOverride2: return pm_override2_;
        case kIdxRbbmStatus:  return kRbbmStatusIdle;
        case kIdxPeriphId1:    return 0u;
        case kIdxPeriphId2:    return 0u;
        case kIdxPatchRelease: return 0u;
    }
    if (auto it = reg_file_.find((a - base_) >> 2); it != reg_file_.end())
        return it->second;
    if (((a - base_) >> 2) == kIdxSqInstStoreManagment)
        return 0u;
    HaltUnsupportedAccess("ReadWord", a, 0);
}

void AdrenoA2xxGpu::WriteWord(uint32_t a, uint32_t v) {
    const uint32_t idx = (a - base_) >> 2;
    written_ = true;
    if ((idx >= kIdxScratchReg0 && idx <= kIdxScratchReg7) || idx == kIdxScratchAddr || idx == kIdxScratchUmsk) {
        WriteRegister(idx, v); return;
    }
    if (emu_.Get<AdrenoA2xxInterrupts>().Owns(idx)) {
        WriteInterruptRegister(idx, v);
        return;
    }
    if (auto& cp = emu_.Get<AdrenoA2xxCommandProcessor>(); cp.Owns(idx)) {
        cp.Write(idx, v);
        return;
    }
    switch ((a - base_) >> 2) {
        case kIdxPmOverride1: pm_override1_ = v; return;
        case kIdxPmOverride2: pm_override2_ = v; return;
        case kIdxSoftReset:       return;
        case kIdxRbbmCntl:        return;
        case kIdxRbbmDebug:
            if (v != kRbbmDebugDriverValue) HaltUnsupportedAccess("RBBM_DEBUG", a, v);
            return;
        case kIdxRbWptrBase:      return;
        case kIdxRbWptrDelay:     return;
        case kIdxMhArbiterConfig: return;
        case kIdxSqVsProgram:
        case kIdxSqPsProgram: WriteRegister(idx, v); return;
        case kIdxMhMmuConfig:
        case kIdxMhMmuVaRange:
        case kIdxMhMmuPtBase:
        case kIdxMhMmuTranError:
        case kIdxMhMmuInvalidate:
        case kIdxMhMmuMpuBase:
        case kIdxMhMmuMpuEnd:     WriteMmuRegister(idx, v); return;
        case kIdxRbEdramInfo: reg_file_[idx] = v; return;
        case kIdxCpDebug:
        case kIdxMeCntl:
        case kIdxMeRamWaddr:
        case kIdxMeRamData:
        case kIdxPfpUcodeAddr:
        case kIdxPfpUcodeData:
        case kIdxQueueThresh:     return;
    }
    HaltUnsupportedAccess("WriteWord", a, v);
}

void AdrenoA2xxGpu::SaveState(StateWriter& w) {
    w.Write("pm_override1", pm_override1_);
    w.Write("pm_override2", pm_override2_);
    emu_.Get<AdrenoA2xxCommandProcessor>().SaveState(w);
    w.Write("reg_file_count", static_cast<uint32_t>(reg_file_.size()));
    for (const auto& [idx, val] : reg_file_) { w.Write("idx", idx); w.Write("val", val); }
    emu_.Get<AdrenoA2xxContext>().SaveState(w);
    emu_.Get<AdrenoA2xxDraw>().SaveState(w);
    emu_.Get<AdrenoA2xxPixelStage>().SaveState(w);
    emu_.Get<AdrenoA2xxInterrupts>().SaveState(w);
}

void AdrenoA2xxGpu::RestoreState(StateReader& r) {
    r.Read("pm_override1", pm_override1_);
    r.Read("pm_override2", pm_override2_);
    emu_.Get<AdrenoA2xxCommandProcessor>().RestoreState(r);
    uint32_t n = 0;
    r.Read("reg_file_count", n);
    reg_file_.clear();
    for (uint32_t k = 0; k < n; ++k) {
        uint32_t idx = 0, val = 0;
        r.Read("idx", idx);
        r.Read("val", val);
        reg_file_[idx] = val;
    }
    RefreshMmu();
    emu_.Get<AdrenoA2xxContext>().RestoreState(r);
    emu_.Get<AdrenoA2xxDraw>().RestoreState(r);
    emu_.Get<AdrenoA2xxPixelStage>().RestoreState(r);
    emu_.Get<AdrenoA2xxInterrupts>().RestoreState(r);
    written_ = true;
}

void AdrenoA2xxGpu::PowerDown() {
    if (!written_) return;
    pm_override1_ = pm_override2_ = 0;
    emu_.Get<AdrenoA2xxCommandProcessor>().PowerOnReset();
    reg_file_.clear();
    RefreshMmu();
    emu_.Get<AdrenoA2xxContext>().PowerOnReset();
    emu_.Get<AdrenoA2xxDraw>().PowerOnReset();
    emu_.Get<AdrenoA2xxPixelQueue>().ResetStage();
    emu_.Get<AdrenoA2xxInterrupts>().PowerOnReset();
    PublishInterruptLine();
    written_ = false;
}

uint32_t AdrenoA2xxGpu::StoredRegister(uint32_t idx) const {
    const auto stored = reg_file_.find(idx);
    return stored == reg_file_.end() ? 0u : stored->second;
}

const AdrenoA2xxMmu& AdrenoA2xxGpu::MmuConfig() const { return mmu_; }

void AdrenoA2xxGpu::RefreshMmu() {
    mmu_.config   = StoredRegister(kIdxMhMmuConfig);
    mmu_.va_range = StoredRegister(kIdxMhMmuVaRange);
    mmu_.pt_base  = StoredRegister(kIdxMhMmuPtBase);
    mmu_.mpu_base = StoredRegister(kIdxMhMmuMpuBase);
    mmu_.mpu_end  = StoredRegister(kIdxMhMmuMpuEnd);
}

void AdrenoA2xxGpu::WriteInterruptRegister(uint32_t idx, uint32_t value) {
    emu_.Get<AdrenoA2xxInterrupts>().Write(idx, value, base_ + idx * 4u);
    PublishInterruptLine();
}

void AdrenoA2xxGpu::WriteMmuRegister(uint32_t idx, uint32_t value) {
    const uint32_t address = base_ + idx * 4u;
    if (idx == kIdxMhMmuInvalidate) {
        if (value & ~kMmuInvalidateDefined)
            HaltUnsupportedAccess("GPU MMU invalidate", address, value);
        return;
    }
    if (idx == kIdxMhMmuConfig) {
        if (value & ~kMmuConfigDefined)
            HaltUnsupportedAccess("GPU MMU configuration", address, value);
        if (value & kMmuEnable) {
            const uint32_t behavior = (value >> kMmuBehaviorShift) & kMmuBehaviorMask;
            uint32_t uniform = 0u;
            for (uint32_t client = 0; client < kMmuClientCount; ++client)
                uniform |= behavior << (kMmuBehaviorShift + client * kMmuBehaviorBits);
            if ((value & kMmuSplitMode) || behavior > kMmuBehaviorTranslateRange ||
                (value & kMmuBehaviorFields) != uniform)
                HaltUnsupportedAccess("GPU MMU configuration", address, value);
        }
    }
    reg_file_[idx] = value;
    RefreshMmu();
}

void AdrenoA2xxGpu::WriteRegister(uint32_t idx, uint32_t value) {
    if (emu_.Get<AdrenoA2xxInterrupts>().Owns(idx)) {
        WriteInterruptRegister(idx, value);
        return;
    }
    switch (idx) {
        case kIdxSqVsProgram: case kIdxSqPsProgram:
            if (value) HaltUnsupportedAccess("unsupported shader program selector", base_ + idx * 4u, value);
            break;
        case kIdxScratchAddr:
            if (value & 31u) HaltUnsupportedAccess("SCRATCH_ADDR unsupported alignment", base_ + idx * 4u, value);
            break;
        case kIdxScratchUmsk:
            if (value > 1u) HaltUnsupportedAccess("SCRATCH_UMSK unsupported mask/swap", base_ + idx * 4u, value);
            break;
        case kIdxPmOverride1: case kIdxPmOverride2: case kIdxRbbmDebug:
            WriteWord(base_ + idx * 4u, value); return;
        case kIdxMhMmuConfig: case kIdxMhMmuVaRange: case kIdxMhMmuPtBase: case kIdxMhMmuTranError:
        case kIdxMhMmuInvalidate: case kIdxMhMmuMpuBase: case kIdxMhMmuMpuEnd:
            WriteMmuRegister(idx, value); return;
        case kIdxSoftReset: case kIdxRbbmCntl:
        case kIdxRbWptrBase: case kIdxRbWptrDelay: case kIdxMhArbiterConfig: case kIdxMhMmuPageFault:
        case kIdxRbCntl: case kIdxRbBase: case kIdxRbRptrAddr: case kIdxRbWptr:
        case kIdxCpDebug:
        case kIdxMeCntl: case kIdxMeRamWaddr: case kIdxMeRamData: case kIdxPfpUcodeAddr:
        case kIdxPfpUcodeData: case kIdxQueueThresh: case kIdxRbbmStatus:
        case kIdxPeriphId1: case kIdxPeriphId2: case kIdxPatchRelease:
            HaltUnsupportedAccess("PM4 unsupported control register write", base_ + idx * 4u, value);
        default: break;
    }
    if (idx == kIdxScratchReg0) {
        const auto mask = reg_file_.find(kIdxScratchUmsk);
        if (mask != reg_file_.end() && mask->second != 0u) {
            const auto address = reg_file_.find(kIdxScratchAddr);
            if (mask->second != 1u || address == reg_file_.end() || (address->second & 31u) != 0)
                HaltUnsupportedAccess("scratch timestamp unsupported configuration", base_ + idx * 4u, value);
            emu_.Get<AdrenoA2xxMemory>().WritePa32(address->second, value, MmuConfig());
        }
    }
    emu_.Get<AdrenoA2xxContext>().ShadowWrite(idx, value, MmuConfig());
    reg_file_[idx] = value;
}

}
