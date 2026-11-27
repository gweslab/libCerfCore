#pragma once

#include <cstdint>

namespace cerfcore {

namespace adreno_a2xx_regs {

/* RBBM_PM_OVERRIDE1/2 (0x39C/0x39D): clock-gating override config. R/W
   store+readback - the guest reads pm_override1 back (kgsl_drawctxt.c:479),
   so a write-only accept would corrupt its context save. */
constexpr uint32_t kIdxPmOverride1 = 0x039Cu;
constexpr uint32_t kIdxPmOverride2 = 0x039Du;

/* RBBM_SOFT_RESET (0x3C): write-only reset pulse (gsl_yamato_imx.c:342 writes
   0xFFFFFFFF then 0, never reads it); the stateless GPU3D model has no pipeline
   to reset. */
constexpr uint32_t kIdxSoftReset = 0x003Cu;

/* RBBM_CNTL (0x3B): init config, write-only (gsl_yamato_imx.c:347 writes it
   once, never reads it). */
constexpr uint32_t kIdxRbbmCntl = 0x003Bu;

/* MH_ARBITER_CONFIG (0xA40): memory-hub arbiter config, write-only on yamato
   (gsl_yamato_imx.c:350 writes it, never reads); CERF models no GPU memory
   arbitration. */
constexpr uint32_t kIdxMhArbiterConfig = 0x0A40u;

constexpr uint32_t kIdxSqVsProgram = 0x21F7u;
constexpr uint32_t kIdxSqPsProgram = 0x21F6u;

constexpr uint32_t kIdxSqProgramCntl         = 0x2180u;
constexpr uint32_t kProgramVsExportModeShift = 24u;
constexpr uint32_t kProgramVsExportModeMask  = 7u;
constexpr uint32_t kProgramGenIndexVertex    = 0x80000000u;
constexpr uint32_t kVsExportKill             = 4u;
constexpr uint32_t kVsExportSpriteKill       = 5u;
constexpr uint32_t kVsExportEdgeKill         = 6u;
constexpr uint32_t kVsExportMultipass        = 7u;

constexpr uint32_t kIdxPaClClipCntl  = 0x2204u;
constexpr uint32_t kClipDisable      = 0x00010000u;
constexpr uint32_t kClipDirectX      = 0x00080000u;
constexpr uint32_t kClipVertexKillOr = 0x00200000u;

constexpr uint32_t kIdxMhMmuConfig     = 0x0040u;
constexpr uint32_t kIdxMhMmuVaRange    = 0x0041u;
constexpr uint32_t kIdxMhMmuPtBase     = 0x0042u;
constexpr uint32_t kIdxMhMmuPageFault  = 0x0043u;
constexpr uint32_t kIdxMhMmuTranError  = 0x0044u;
constexpr uint32_t kIdxMhMmuInvalidate = 0x0045u;

constexpr uint32_t kIdxMhInterruptMask = 0x0A42u;

constexpr uint32_t kIdxRbbmDebug        = 0x039Bu;
constexpr uint32_t kRbbmDebugDriverValue = 0x00080000u;

constexpr uint32_t kIdxMhInterruptStatus = 0x0A43u;
constexpr uint32_t kIdxMhInterruptClear  = 0x0A44u;
constexpr uint32_t kIdxCpIntStatus       = 0x01F3u;
constexpr uint32_t kIdxRbbmIntStatus     = 0x03B5u;
constexpr uint32_t kIdxRbbmIntAck        = 0x03B6u;
constexpr uint32_t kIdxSqIntCntl         = 0x0D34u;
constexpr uint32_t kIdxSqIntStatus       = 0x0D35u;
constexpr uint32_t kIdxSqIntAck          = 0x0D36u;

constexpr uint32_t kIdxRbbmIntCntl = 0x03B4u;  /* RBBM_INT_CNTL, kgsl_yamato.c:34 */
constexpr uint32_t kIdxCpIntCntl   = 0x01F2u;  /* CP_INT_CNTL, kgsl_ringbuffer.c */
constexpr uint32_t kIdxRbWptrBase  = 0x01C8u;  /* RB_WPTR_BASE, kgsl_ringbuffer_start sub_C101D86C */
constexpr uint32_t kIdxRbWptrDelay = 0x01C6u;  /* RB_WPTR_DELAY */
constexpr uint32_t kIdxRbCntl      = 0x01C1u;  /* RB_CNTL, kgsl_ringbuffer_start sub_C101D86C (RMW) */

/* RBBM_STATUS (0x5D0): the idle poll. kgsl_yamato_idle waits for GUI_ACTIVE
   (bit31) clear (gsl_yamato_imx.c:769) / status ==0x110 (kgsl_yamato.c:792);
   CERF's GPU has no async pipeline, so it is always idle. */
constexpr uint32_t kIdxRbbmStatus     = 0x05D0u;
constexpr uint32_t kRbbmStatusIdle    = 0x00000110u;

constexpr uint32_t kIdxMasterIntSignal = 0x03B7u;

/* SQ_INST_STORE_MANAGMENT (0xD02, a2xx.xml:1237): shader-store partitioning; the draw-context
   save reads it before any write (kgsl_drawctxt.c:1277), so an unwritten read is the power-on
   default (a2xx.xml documents no reset; no shader instruction store modelled -> 0). */
constexpr uint32_t kIdxSqInstStoreManagment = 0x0D02u;

/* NXP linux-imx a1638da9, yamato/22/yamato_offset.h:450-465. */
constexpr uint32_t kIdxScratchReg0 = 0x0578u;
constexpr uint32_t kIdxScratchReg2 = 0x057Au;
constexpr uint32_t kIdxScratchReg7 = 0x057Fu;

constexpr uint32_t kIdxMhMmuMpuBase = 0x0046u;
constexpr uint32_t kIdxMhMmuMpuEnd  = 0x0047u;

/* Chip-id read triple PERIPHID1/PERIPHID2/PATCH_RELEASE (0x3F9/0x3FA/0x1):
   getchipid packs these into chip_id, which getproperty copies to devinfo
   non-branching (gsl_yamato_imx.c:556) - nothing gates on it, so 0 is faithful
   (the real PERIPHID values are off-disk). */
constexpr uint32_t kIdxPeriphId1    = 0x03F9u;
constexpr uint32_t kIdxPeriphId2    = 0x03FAu;
constexpr uint32_t kIdxPatchRelease = 0x0001u;

constexpr uint32_t kIdxRbEdramInfo  = 0x0F02u;  /* RB_EDRAM_INFO, yamato_reg.h:350 */
constexpr uint32_t kIdxRbBase       = 0x01C0u;  /* REG_CP_RB_BASE, yamato_reg.h:283 */
constexpr uint32_t kIdxRbRptrAddr   = 0x01C3u;  /* REG_CP_RB_RPTR_ADDR, yamato_reg.h:286 */
constexpr uint32_t kIdxRbWptr       = 0x01C5u;  /* REG_CP_RB_WPTR, yamato_reg.h:288 */
constexpr uint32_t kIdxScratchAddr  = 0x01DDu;  /* REG_SCRATCH_ADDR, yamato_reg.h:354 */
constexpr uint32_t kIdxScratchUmsk  = 0x01DCu;  /* REG_SCRATCH_UMSK, yamato_reg.h:357 */
constexpr uint32_t kIdxCpIntAck     = 0x01F4u;  /* REG_CP_INT_ACK, yamato_reg.h:273 */
constexpr uint32_t kIdxCpDebug      = 0x01FCu;  /* REG_CP_DEBUG, yamato_reg.h:268 */
constexpr uint32_t kIdxMeCntl       = 0x01F6u;  /* REG_CP_ME_CNTL, yamato_reg.h:276 */
constexpr uint32_t kIdxMeRamWaddr   = 0x01F8u;  /* REG_CP_ME_RAM_WADDR, yamato_reg.h:278 */
constexpr uint32_t kIdxMeRamData    = 0x01FAu;  /* REG_CP_ME_RAM_DATA, yamato_reg.h:277 */
constexpr uint32_t kIdxPfpUcodeAddr = 0x00C0u;  /* REG_CP_PFP_UCODE_ADDR, yamato_reg.h:280 */
constexpr uint32_t kIdxPfpUcodeData = 0x00C1u;  /* REG_CP_PFP_UCODE_DATA, yamato_reg.h:281 */
constexpr uint32_t kIdxQueueThresh  = 0x01D5u;  /* REG_CP_QUEUE_THRESHOLDS, yamato_reg.h:282 */

/* PM4 packet decode (kgsl_pm4types.h): type=hdr>>30; TYPE3 opcode=(hdr>>8)&0xFF,
   cnt=((hdr>>16)&0x3FFF)+1; TYPE0 writes cnt regs from regindx=hdr&0x7FFF. */
constexpr uint32_t kPm4Type0 = 0u;
constexpr uint32_t kPm4Type2 = 2u;
constexpr uint32_t kPm4Type3 = 3u;
constexpr uint32_t kPm4OpMeInit              = 0x48u;  /* PM4_ME_INIT */
constexpr uint32_t kPm4OpNop                 = 0x10u;  /* PM4_NOP */
constexpr uint32_t kPm4OpIndirectBuffer      = 0x3Fu;  /* PM4_INDIRECT_BUFFER */
constexpr uint32_t kPm4OpIndirectBufferPfd   = 0x37u;  /* PM4_INDIRECT_BUFFER_PFD */
constexpr uint32_t kPm4OpWaitForIdle         = 0x26u;  /* PM4_WAIT_FOR_IDLE */
constexpr uint32_t kPm4OpEventWrite          = 0x46u;  /* PM4_EVENT_WRITE */
constexpr uint32_t kPm4OpInterrupt           = 0x40u;  /* PM4_INTERRUPT */
constexpr uint32_t kPm4OpCondExec            = 0x44u;
constexpr uint32_t kPm4OpSetProtectedMode    = 0x5Fu;
constexpr uint32_t kPm4OpSetBinMask          = 0x50u;
constexpr uint32_t kPm4OpSetBinSelect        = 0x51u;
constexpr uint32_t kPm4OpWaitRegMem          = 0x3Cu;
constexpr uint32_t kWaitRegMemPayloadDwords  = 5u;
constexpr uint32_t kCpCondFunctionMask       = 0x7u;
constexpr uint32_t kCpCondAlways             = 0u;
constexpr uint32_t kCpCondLess               = 1u;
constexpr uint32_t kCpCondLessEqual          = 2u;
constexpr uint32_t kCpCondEqual              = 3u;
constexpr uint32_t kCpCondNotEqual           = 4u;
constexpr uint32_t kCpCondGreaterEqual       = 5u;
constexpr uint32_t kCpCondGreater            = 6u;
constexpr uint32_t kMeInitFieldMask           = 0x3FFu;
constexpr uint32_t kMeInitFieldCount          = 10u;
constexpr uint32_t kMeInitFieldRealTimeStream = 0u;
constexpr uint32_t kMeInitFieldImplicitSync   = 1u;
constexpr uint32_t kMeInitFieldRegisterOffsets = 2u;
constexpr uint32_t kMeInitRegisterOffsetDwords = 8u;
constexpr uint32_t kMeInitRegisterOffsetStride = 0x80u;
constexpr uint32_t kMeInitFieldShaderBases    = 3u;
constexpr uint32_t kMeInitFieldMaxContexts    = 4u;
constexpr uint32_t kMeInitFieldWriteConfirm   = 5u;
constexpr uint32_t kMeInitFieldMemorySwap     = 6u;
constexpr uint32_t kMeInitFieldProtectedMode  = 7u;
constexpr uint32_t kMeInitFieldHeaderDump     = 8u;
constexpr uint32_t kMeInitFieldHeaderDumpSize = 9u;
constexpr uint32_t kProtectCheckEnable       = 0x20000000u;
constexpr uint32_t kProtectLimitMask         = 0x3FFFu;
constexpr uint32_t kProtectedModeOff         = 0u;
constexpr uint32_t kProtectedModeOn          = 1u;
constexpr uint32_t kProtectedModeUnknown     = 2u;
constexpr uint64_t kBinWordReset             = 0xFFFFFFFFull;
constexpr uint32_t kPm4OpSetConstant         = 0x2Du;  /* PM4_SET_CONSTANT */
constexpr uint32_t kPm4OpLoadConstantContext = 0x2Eu;  /* PM4_LOAD_CONSTANT_CONTEXT */
constexpr uint32_t kPm4OpRegToMem            = 0x3Eu;  /* PM4_REG_TO_MEM, kgsl_pm4types.h:68 */
constexpr uint32_t kPm4OpMemWrite            = 0x3Du;  /* NXP gsl_pm4types.h: PM4_MEM_WRITE. */
constexpr uint32_t kPm4OpImStore             = 0x2Cu;  /* PM4_IM_STORE, kgsl_pm4types.h:148 */
constexpr uint32_t kPm4OpRegRmw              = 0x21u;  /* PM4_REG_RMW, kgsl_pm4types.h:65 */
constexpr uint32_t kPm4OpWaitRegEq           = 0x52u;  /* PM4_WAIT_REG_EQ, kgsl_pm4types.h:53 */
constexpr uint32_t kPm4OpImLoadImmediate     = 0x2Bu;  /* PM4_IM_LOAD_IMMEDIATE, kgsl_pm4types.h:121 */
constexpr uint32_t kPm4OpImLoad              = 0x27u;  /* PM4_IM_LOAD, kgsl_pm4types.h:118 */
constexpr uint32_t kPm4OpDrawIndx            = 0x22u;  /* PM4_DRAW_INDX, kgsl_pm4types.h:96 */
constexpr uint32_t kPm4OpInvalidateState     = 0x3Bu;  /* PM4_INVALIDATE_STATE, kgsl_pm4types.h:127 */
constexpr uint32_t kPm4OpSetShaderBases      = 0x4Au;  /* PM4_SET_SHADER_BASES, kgsl_pm4types.h:131 */
constexpr uint32_t kEventCacheFlushTs        = 4u;     /* CACHE_FLUSH_TS, yamato_reg.h:27 */
constexpr uint32_t kEventCacheFlush          = 6u;     /* CACHE_FLUSH, yamato_reg.h:29 */
constexpr uint32_t kRegToMemShadowFlag       = 1u << 30;  /* build_reg_to_mem_range flag, kgsl_drawctxt.c:439 */

/* SET_CONSTANT bank bases: the type field ((tgt>>16)&7) selects a constant bank whose
   values the draw-context save reads back as registers (reg_to_mem). type 4 = register
   0x2000+offset (GSL_HAL_SUBBLOCK_OFFSET, gsl_yamato_imx.c:286 / kgsl_drawctxt.c:360). */
constexpr uint32_t kScBaseAlu   = 0x4000u;  /* type 0: REG_SQ_CONSTANT_0, yamato_reg.h:392 */
constexpr uint32_t kScBaseFetch = 0x4800u;  /* type 1: REG_SQ_FETCH_0, yamato_reg.h:393 */
constexpr uint32_t kScBaseBool  = 0x4900u;  /* type 2: REG_SQ_CF_BOOLEANS, yamato_reg.h:359 */
constexpr uint32_t kScBaseLoop  = 0x4908u;  /* type 3: REG_SQ_CF_LOOP, yamato_reg.h:360 */
constexpr uint32_t kScBaseReg   = 0x2000u;  /* type 4: register file */

}  // namespace adreno_a2xx_regs

}
