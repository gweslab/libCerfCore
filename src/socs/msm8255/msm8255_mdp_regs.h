#pragma once

#include <array>
#include <cstdint>

namespace cerfcore::msm8255_mdp {

inline constexpr uint32_t kBase = 0xA3F00000u;
inline constexpr uint32_t kSize = 0x00100000u;

inline constexpr uint32_t kRegDispIntfSel     = 0x00038u;
inline constexpr uint32_t kRegMddiLdParam     = 0x00090u;
inline constexpr uint32_t kRegMddiVideoPacket = 0x00094u;
inline constexpr uint32_t kRegMddiEnable      = 0x00098u;
inline constexpr uint32_t kRegOvlp0Kick       = 0x00004u;
inline constexpr uint32_t kRegDmaSKick        = 0x00010u;

inline constexpr uint32_t kOvlp0          = 0x10000u;
inline constexpr uint32_t kOvlp1          = 0x18000u;
inline constexpr uint32_t kOvlpCfg        = 0x004u;
inline constexpr uint32_t kOvlpSize       = 0x008u;
inline constexpr uint32_t kOvlpBase       = 0x00Cu;
inline constexpr uint32_t kOvlpStride     = 0x010u;
inline constexpr uint32_t kOvlpOpMode     = 0x014u;
inline constexpr uint32_t kOvlpStage0     = 0x104u;
inline constexpr uint32_t kOvlpStageStride = 0x20u;
inline constexpr uint32_t kOvlpStageCount = 4u;
inline constexpr uint32_t kOvlpStageEnd   = 0x18Cu;
inline constexpr uint32_t kOvlpBaseTransp = 0x180u;
inline constexpr uint32_t kOvlpCo3        = 0x1004u;
inline constexpr uint32_t kOvlpCo3Stride  = 0x400u;

inline constexpr uint32_t kStageOp       = 0x00u;
inline constexpr uint32_t kStageFgAlpha  = 0x04u;
inline constexpr uint32_t kStageBgAlpha  = 0x08u;
inline constexpr uint32_t kStageTransp   = 0x0Cu;

inline constexpr uint32_t kTranspLow0  = 0x0u;
inline constexpr uint32_t kTranspLow1  = 0x4u;
inline constexpr uint32_t kTranspHigh0 = 0x8u;
inline constexpr uint32_t kTranspHigh1 = 0xCu;

inline constexpr uint32_t kRegLayermixerInCfg = 0x10100u;

inline constexpr uint32_t kDmaP         = 0x90000u;
inline constexpr uint32_t kDmaS         = 0xA0000u;
inline constexpr uint32_t kDmaConfig    = 0x00u;
inline constexpr uint32_t kDmaSrcSize   = 0x04u;
inline constexpr uint32_t kDmaSrcBase   = 0x08u;
inline constexpr uint32_t kDmaSrcStride = 0x0Cu;
inline constexpr uint32_t kDmaDstXy     = 0x10u;
inline constexpr uint32_t kDmaPColor    = 0x90070u;
inline constexpr uint32_t kDmaSColor    = 0xA0028u;
inline constexpr uint32_t kDmaPLut      = 0x94800u;
inline constexpr uint32_t kDmaPLutEnd   = 0x94FFCu;
inline constexpr uint32_t kLutBankStride = 0x400u;

inline constexpr uint32_t kPipeBase    = 0x20000u;
inline constexpr uint32_t kPipeStride  = 0x10000u;
inline constexpr uint32_t kPipeCount   = 4u;
inline constexpr uint32_t kVgPipeCount = 2u;

inline constexpr uint32_t kPipeSrcSize    = 0x00u;
inline constexpr uint32_t kPipeSrcXy      = 0x04u;
inline constexpr uint32_t kPipeDstSize    = 0x08u;
inline constexpr uint32_t kPipeDstXy      = 0x0Cu;
inline constexpr uint32_t kPipeSrcp0      = 0x10u;
inline constexpr uint32_t kPipeSrcp1      = 0x14u;
inline constexpr uint32_t kPipeSrcp2      = 0x18u;
inline constexpr uint32_t kPipeStrideA    = 0x40u;
inline constexpr uint32_t kPipeStrideB    = 0x44u;
inline constexpr uint32_t kPipeFrameSize  = 0x48u;
inline constexpr uint32_t kPipeFormat     = 0x50u;
inline constexpr uint32_t kPipeUnpack     = 0x54u;
inline constexpr uint32_t kPipeOpMode     = 0x58u;
inline constexpr uint32_t kPipePhaseX     = 0x5Cu;
inline constexpr uint32_t kPipePhaseY     = 0x60u;
inline constexpr uint32_t kPipeFetchConfig = 0x1004u;
inline constexpr uint32_t kPipeSolidColor = 0x1008u;
inline constexpr uint32_t kPipeIgcC01     = 0x5000u;
inline constexpr uint32_t kPipeIgcC2      = 0x5800u;
inline constexpr uint32_t kPipeIgcEnd     = 0x3FCu;
inline constexpr uint32_t kPipeCsc        = 0x4400u;
inline constexpr uint32_t kPipeCscEnd     = 0x4420u;
inline constexpr uint32_t kPipeCscPreBias  = 0x4500u;
inline constexpr uint32_t kPipeCscPostBias = 0x4580u;
inline constexpr uint32_t kPipeCscPreClamp  = 0x4600u;
inline constexpr uint32_t kPipeCscPostClamp = 0x4680u;
inline constexpr uint32_t kPipeQseedConfig = 0x8100u;
inline constexpr uint32_t kPipeQseedSelect = 0x8200u;
inline constexpr uint32_t kPipeQseedTable  = 0x9000u;
inline constexpr uint32_t kPipeQseedTableEnd = 0xAFFCu;

inline constexpr uint32_t kHistogram     = 0x95000u;
inline constexpr uint32_t kHistogramSize = 0x400u;

inline constexpr std::array<uint32_t, 4> kBpcBits      = {4u, 5u, 6u, 8u};
inline constexpr std::array<uint32_t, 4> kBpcAlphaBits = {1u, 4u, 6u, 8u};

}
