#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "../irq_controller.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include "msm8255_mdp_frame_timing.h"
#include "msm8255_mdp_histogram.h"
#include "msm8255_mdp_output.h"
#include "msm8255_mdp_regs.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <span>

namespace cerfcore {

namespace {

using namespace msm8255_mdp;

constexpr uint32_t kWordCount = kSize / 4u;

/* Linux arch/arm/mach-msm video-msm mdp.c mdp_probe reads the word at the MDP
   base as mdp_version and tests it against the packed 0x04030303, one byte per
   version field. */
constexpr uint32_t kRegVersion   = 0x00000u;
constexpr uint32_t kVersionValue = 0x04000000u;

/* Linux arch/arm/mach-msm video-msm mdp.h, the CONFIG_FB_MSM_MDP40 arm:
   MDP_EBI2_PORTMAP_MODE. */
constexpr uint32_t kRegEbi2PortmapMode = 0x00070u;

/* Linux arch/arm/mach-msm video-msm mdp.h, the CONFIG_FB_MSM_MDP40 arm:
   MDP_INTR_ENABLE, MDP_INTR_STATUS and MDP_INTR_CLEAR. */
constexpr uint32_t kRegIntrEnable = 0x00050u;
constexpr uint32_t kRegIntrStatus = 0x00054u;
constexpr uint32_t kRegIntrClear  = 0x00058u;

/* Linux arch/arm/mach-msm video-msm mdp4_util.c mdp4_display_status reads
   MDP_DISPLAY_STATUS and keeps its low ten bits. */
constexpr uint32_t kRegDispStatus = 0x00018u;

/* Linux arch/arm/mach-msm video-msm mdp_vsync.c: MDP_VSYNC_SEL. */
constexpr uint32_t kRegVsyncSel = 0x00124u;

/* Linux drivers/gpu/drm/msm registers display mdp4.xml: the LCDC array's
   ENABLE register, whose bit 0 mdp4_util.c tests before clearing the block. */
constexpr uint32_t kRegLcdcEnable = 0xC0000u;

/* Linux arch/arm/mach-msm video-msm mdp_vsync.c, its CONFIG_FB_MSM_MDP40 arm:
   MDP_SYNC_CFG_0 and MDP_SYNC_CFG_1, which mdp_set_sync_cfg_0 and _1 build from
   the line count, the external-vsync enable and the vsync counter. */
constexpr uint32_t kRegSyncCfg0 = 0x00100u;
constexpr uint32_t kRegSyncCfg1 = 0x00104u;

constexpr uint32_t kReg020C = 0x0020Cu;
constexpr uint32_t kReg0040 = 0x00040u;

constexpr uint32_t kRegVsyncInit0   = 0x00128u;
constexpr uint32_t kRegVsyncInit1   = 0x0012Cu;
constexpr uint32_t kRegSyncThresh2  = 0x00208u;
constexpr uint32_t kRegStartPos2    = 0x00218u;
constexpr uint32_t kTearEnableBits  = 3u;

/* Linux arch/arm/mach-msm irqs-7x30.h: INT_MDP. */
constexpr uint32_t kVicLine = 80u;

constexpr uint32_t kRegOverlayOp0 = kOvlp0 + kOvlpOpMode;
constexpr uint32_t kRegOverlayOp1 = kOvlp1 + kOvlpOpMode;
constexpr uint32_t kRegOverlay0Stage0Op = kOvlp0 + kOvlpStage0;
constexpr uint32_t kRegOverlay1Stage0Op = kOvlp1 + kOvlpStage0;

/* Linux drivers/gpu/drm/msm registers display mdp4.xml: OVERLAY_FLUSH, one
   boolean per block in the order OVLP0, OVLP1, VG1, VG2, RGB1, RGB2. */
constexpr uint32_t kRegOverlayFlush = 0x18000u;

/* Linux arch/arm/mach-msm video-msm mdp4.h: INTR_OVERLAY0_DONE, which
   mdp4_util.c mdp4_isr dispatches through its MDDI arm. */
constexpr uint32_t kIntrOverlay0Done = 0x00000001u;

/* Linux arch/arm/mach-msm video-msm mdp4.h: INTR_DMA_S_DONE, the bit
   mdp4_util.c mdp4_isr completes the DMA_S block on. */
constexpr uint32_t kIntrDmaSDone = 0x00000004u;

constexpr uint32_t kIntrDmaPDone      = 1u << 4;
constexpr uint32_t kIntrDmaPHistogram = 1u << 17;

constexpr uint32_t kDispStatusOverlay0Busy = 1u << 0;
constexpr uint32_t kDispStatusDmaSBusy     = 1u << 3;

constexpr uint32_t kSyncRegisters[] = {0x00118u, 0x0011Cu, kRegVsyncSel, 0x00200u,
                                       0x00204u, kReg020C, 0x00210u,     0x00214u};

constexpr uint32_t kSyncConfigRegisters[] = {0x00118u, 0x0011Cu,        kRegVsyncInit0,
                                             kRegVsyncInit1, 0x00200u,  0x00204u,
                                             kRegSyncThresh2, 0x00210u, 0x00214u,
                                             kRegStartPos2};

constexpr uint32_t kRegReset = 0u;

constexpr uint32_t kRegSwReset  = 0x0001Cu;
constexpr uint32_t kSwResetBits = 0x1Fu;

constexpr uint32_t kStateChunkWords = 1024u;
static_assert(kWordCount % kStateChunkWords == 0u,
              "the register file must divide into whole state chunks");

struct Span {
    uint32_t first;
    uint32_t last;
};

constexpr Span kWritableSpans[] = {
    {0x00028u, 0x00028u}, {0x00030u, 0x00030u},
    {kRegDispIntfSel, kRegDispIntfSel}, {kReg0040, kReg0040}, {0x00048u, 0x00048u},
    {0x00060u, 0x00060u}, {0x00068u, 0x00068u},
    {0x00070u, 0x00070u}, {0x00090u, 0x00090u}, {0x00094u, 0x00094u},
    {0x00098u, 0x00098u},
    {kRegSyncCfg0, kRegSyncCfg0}, {kRegSyncCfg1, kRegSyncCfg1},
    {0x00118u, 0x00118u}, {0x0011Cu, 0x0011Cu}, {kRegVsyncSel, kRegVsyncSel},
    {kRegVsyncInit0, kRegVsyncInit1},
    {0x00200u, 0x00200u}, {0x00204u, 0x00204u}, {kRegSyncThresh2, kRegSyncThresh2},
    {kReg020C, kReg020C}, {0x00210u, 0x00210u}, {0x00214u, 0x00214u},
    {kRegStartPos2, kRegStartPos2},
    {0x0021Cu, 0x0021Cu}, {0x00220u, 0x00220u},
    {kOvlp0 + kOvlpCfg, kOvlp0 + kOvlpStride},
    {kRegOverlayOp0, kRegOverlayOp0}, {kRegOverlayOp1, kRegOverlayOp1},
    {kRegLayermixerInCfg, kRegLayermixerInCfg},
    {kRegOverlay0Stage0Op, kOvlp0 + kOvlpStageEnd},
    {kRegOverlay1Stage0Op, kOvlp1 + kOvlpStageEnd},
    {kRegOverlayFlush, kRegOverlayFlush},
    {kOvlp0 + kOvlpCo3, kOvlp0 + kOvlpCo3},
    {kOvlp0 + kOvlpCo3 + kOvlpCo3Stride, kOvlp0 + kOvlpCo3 + kOvlpCo3Stride},
    {kOvlp0 + kOvlpCo3 + 2u * kOvlpCo3Stride, kOvlp0 + kOvlpCo3 + 2u * kOvlpCo3Stride},
    {0x91004u, 0x91004u},
    {kDmaP, kDmaP + kDmaDstXy}, {0x90018u, 0x90020u}, {0x90040u, 0x9004Cu},
    {0x90060u, kDmaPColor},
    {kDmaS, kDmaS + kDmaDstXy},
    {kDmaS + 0x18u, kDmaS + 0x18u},
    {0x93400u, 0x93420u}, {0x93500u, 0x93508u}, {0x93580u, 0x93588u},
    {0x93600u, 0x93614u}, {0x93680u, 0x93694u},
    {kDmaPLut, kDmaPLutEnd},
    {0xC0000u, 0xC0038u}, {0xC2000u, 0xC2010u},
};

constexpr Span kPipeSpans[] = {
    {kPipeSrcSize, kPipeDstXy}, {kPipeSrcp0, kPipeSrcp0}, {kPipeStrideA, kPipeStrideA},
    {kPipeFormat, kPipePhaseY}, {kPipeFetchConfig, kPipeSolidColor},
    {kPipeIgcC01, kPipeIgcC01 + kPipeIgcEnd}, {kPipeIgcC2, kPipeIgcC2 + kPipeIgcEnd},
};

constexpr Span kVgPipeSpans[] = {
    {kPipeSrcp1, 0x001Cu}, {kPipeStrideB, kPipeFrameSize},
    {0x2004u, 0x2004u}, {0x2010u, 0x2014u},
    {kPipeCsc, kPipeCscEnd}, {kPipeCscPreBias, kPipeCscPreBias + 8u},
    {kPipeCscPostBias, kPipeCscPostBias + 8u}, {kPipeCscPreClamp, kPipeCscPreClamp + 0x14u},
    {kPipeCscPostClamp, kPipeCscPostClamp + 0x14u},
    {kPipeQseedConfig, kPipeQseedConfig + 0xCu}, {kPipeQseedSelect, kPipeQseedSelect + 4u},
    {kPipeQseedTable, kPipeQseedTableEnd},
};

class Msm8255Mdp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        hist_   = &emu_.Get<Msm8255MdpHistogram>();
        timing_ = &emu_.Get<Msm8255MdpFrameTiming>();
        timing_->Attach([this](uint32_t off) { return Reg(off); },
                        [this](uint32_t bits) { RaiseStatus(bits); });
        ResetState();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            ResetState();
            PublishLine();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == kRegVersion) {
            return kVersionValue;
        }
        if (off == kRegSwReset) {
            return 0u;
        }
        if (Msm8255MdpHistogram::Owns(off)) {
            return hist_->Read(off);
        }
        if (off == kRegDispStatus) {
            return DisplayStatus();
        }
        if (off == kRegIntrEnable || off == kRegIntrStatus || off == kReg0040 ||
            off == kRegDispIntfSel ||
            off == kDmaP || off == kDmaS || off == kDmaPColor ||
            off == kRegVsyncSel || off == kRegLcdcEnable ||
            off == kRegEbi2PortmapMode ||
            off == kRegSyncCfg0 || off == kRegSyncCfg1 || off == kReg020C ||
            off == kRegOverlayOp0 || off == kRegOverlayOp1 ||
            off == kRegLayermixerInCfg || IsPipeOpMode(off) ||
            off == kRegOverlay0Stage0Op || off == kRegOverlay1Stage0Op ||
            std::find(std::begin(kSyncConfigRegisters), std::end(kSyncConfigRegisters), off) !=
                std::end(kSyncConfigRegisters)) {
            return Reg(off);
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == kRegIntrEnable) {
            SetReg(kRegIntrEnable, value);
            PublishLine();
            return;
        }
        if (off == kRegSwReset) {
            if ((value & ~kSwResetBits) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
            return;
        }
        /* Linux arch/arm/mach-msm video-msm mdp4_util.c mdp4_isr writes the
           status word it has just read to MDP_INTR_CLEAR, so a set bit clears
           that source. */
        if (off == kRegIntrClear) {
            SetReg(kRegIntrStatus, Reg(kRegIntrStatus) & ~value);
            PublishLine();
            return;
        }
        if (Msm8255MdpHistogram::Owns(off)) {
            hist_->Write(off, value);
            RaiseStatus(0u);
            return;
        }
        if (off == kRegOvlp0Kick || off == kRegDmaSKick) {
            Kick(off);
            return;
        }
        if (!IsWritable(off) || (off == kReg020C && (value & ~kTearEnableBits) != 0u)) {
            HaltUnsupportedAccess("WriteWord", addr, value);
        }
        const uint32_t old = Reg(off);
        SetReg(off, value);
        if (off == kRegSyncCfg0 || off == kRegSyncCfg1) {
            timing_->SyncConfigWritten(off == kRegSyncCfg0 ? kMsm8255MdpSyncPrimary
                                                           : kMsm8255MdpSyncSecondary, old);
            return;
        }
        if (std::find(std::begin(kSyncRegisters), std::end(kSyncRegisters), off) !=
            std::end(kSyncRegisters)) {
            timing_->SyncRegisterWritten();
        }
    }

    void SaveState(StateWriter& w) override {
        uint32_t chunk[kStateChunkWords];
        for (uint32_t base = 0; base < kWordCount; base += kStateChunkWords) {
            for (uint32_t i = 0; i < kStateChunkWords; ++i) {
                chunk[i] = regs_[base + i].load(std::memory_order_acquire);
            }
            w.WriteBytes("chunk", chunk, sizeof(chunk));
        }
        hist_->SaveState(w);
        timing_->SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        uint32_t chunk[kStateChunkWords];
        for (uint32_t base = 0; base < kWordCount; base += kStateChunkWords) {
            r.ReadBytes("chunk", chunk, sizeof(chunk));
            for (uint32_t i = 0; i < kStateChunkWords; ++i) {
                regs_[base + i].store(chunk[i], std::memory_order_release);
            }
        }
        hist_->RestoreState(r);
        timing_->RestoreState(r);
    }

    void PostRestore() override {
        timing_->PostRestore();
        PublishLine();
    }

private:
    void Kick(uint32_t off) {
        const auto reg   = [this](uint32_t r) { return Reg(r); };
        auto&      output = emu_.Get<Msm8255MdpOutput>();
        if (off == kRegDmaSKick) {
            timing_->RequireIdle(kMsm8255MdpSyncSecondary);
            timing_->Start(kMsm8255MdpSyncSecondary, output.PresentDmaS(reg), kIntrDmaSDone);
            return;
        }
        timing_->RequireIdle(kMsm8255MdpSyncPrimary);
        const uint32_t link_bytes = output.PresentOverlay0(reg);
        if (link_bytes == 0u) {
            RaiseStatus(kIntrOverlay0Done);
            return;
        }
        timing_->Start(kMsm8255MdpSyncPrimary, link_bytes, kIntrOverlay0Done | kIntrDmaPDone);
    }

    uint32_t DisplayStatus() const {
        return (timing_->Busy(kMsm8255MdpSyncPrimary) ? kDispStatusOverlay0Busy : 0u) |
               (timing_->Busy(kMsm8255MdpSyncSecondary) ? kDispStatusDmaSBusy : 0u);
    }

    uint32_t Reg(uint32_t off) const {
        return regs_[off / 4u].load(std::memory_order_acquire);
    }

    /* Linux arch/arm/mach-msm video-msm mdp4_util.c mdp4_isr takes the pending
       set as MDP_INTR_STATUS masked by MDP_INTR_ENABLE, so the line follows
       that product. */
    void PublishLine() {
        auto& vic = emu_.Get<IrqController>();
        if ((Reg(kRegIntrStatus) & Reg(kRegIntrEnable)) != 0u) {
            vic.AssertIrq(kVicLine);
        } else {
            vic.DeAssertIrq(kVicLine);
        }
    }

    void SetReg(uint32_t off, uint32_t value) {
        regs_[off / 4u].store(value, std::memory_order_release);
    }

    void RaiseStatus(uint32_t bits) {
        if (hist_->TakeInterrupt()) bits |= kIntrDmaPHistogram;
        if (bits == 0u) return;
        SetReg(kRegIntrStatus, Reg(kRegIntrStatus) | bits);
        PublishLine();
    }

    void ResetState() {
        for (uint32_t i = 0; i < kWordCount; ++i) {
            regs_[i].store(kRegReset, std::memory_order_release);
        }
    }

    static bool InSpans(uint32_t off, std::span<const Span> spans) {
        for (const Span& s : spans) {
            if (off >= s.first && off <= s.last) return true;
        }
        return false;
    }

    static bool InPipe(uint32_t off, uint32_t& pipe, uint32_t& pipe_off) {
        if (off < kPipeBase || off - kPipeBase >= kPipeCount * kPipeStride) return false;
        pipe     = (off - kPipeBase) / kPipeStride;
        pipe_off = (off - kPipeBase) % kPipeStride;
        return true;
    }

    static bool IsPipeOpMode(uint32_t off) {
        uint32_t pipe = 0u, pipe_off = 0u;
        return InPipe(off, pipe, pipe_off) && pipe_off == kPipeOpMode;
    }

    static bool IsWritable(uint32_t off) {
        if (InSpans(off, kWritableSpans)) return true;
        uint32_t pipe = 0u, pipe_off = 0u;
        if (!InPipe(off, pipe, pipe_off)) return false;
        return InSpans(pipe_off, kPipeSpans) || (pipe < kVgPipeCount && InSpans(pipe_off, kVgPipeSpans));
    }

    std::atomic<uint32_t> regs_[kWordCount] = {};
    Msm8255MdpHistogram* hist_ = nullptr;
    Msm8255MdpFrameTiming* timing_ = nullptr;
};

}

REGISTER_SERVICE(Msm8255Mdp);

}
