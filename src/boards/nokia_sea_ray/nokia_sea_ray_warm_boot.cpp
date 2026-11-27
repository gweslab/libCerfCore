#include "../../socs/msm8255/msm8255_warm_boot.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../execution/engine_bridge.h"
#include "nokia_sea_ray_family_id.h"
#include "nokia_sea_ray_oal_args_layout.h"

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/cpu_state.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kContextPa = kNokiaSeaRayArgsPa + kNokiaSeaRayArgsCollapseContext;

constexpr uint32_t kSavedCpsrWord     = 19u;
constexpr uint32_t kModeRegisterWords = 20u;
constexpr uint32_t kCalleeSavedFirst  = 4u;
constexpr uint32_t kCalleeSavedCount  = 9u;
constexpr uint32_t kReturnFrameWords  = 11u;
constexpr uint32_t kResumedResult     = 1u;
constexpr uint32_t kCpsrThumbBit      = 1u << 5;
constexpr uint32_t kThumbAddressBit   = 1u;
constexpr uint32_t kArmAddressMask    = ~3u;
constexpr uint32_t kThumbAddressMask  = ~1u;

class NokiaSeaRayWarmBoot : public Msm8255WarmBoot {
public:
    using Msm8255WarmBoot::Msm8255WarmBoot;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void ResumeCollapsedCore() override {
        uint32_t va = emu_.Get<EmulatedMemory>().ReadWord(kContextPa);
        va = SkipSaveBlock(va);
        va = SkipSaveBlock(va);
        const uint32_t saved_cpsr = Word(va + 4u * kSavedCpsrWord);
        const uint32_t frame      = va + 4u * kModeRegisterWords;
        uint32_t       callee_saved[kCalleeSavedCount];
        for (uint32_t i = 0; i < kCalleeSavedCount; ++i) callee_saved[i] = Word(frame + 4u * i);
        const uint32_t lr    = Word(frame + 4u * kCalleeSavedCount);
        const uint32_t pc    = Word(frame + 4u * (kCalleeSavedCount + 1u));
        const bool     thumb = (pc & kThumbAddressBit) != 0u;

        auto& cpu = emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmCpu>();
        cerfengine::ArmPsrFull psr;
        psr.word = (saved_cpsr & ~kCpsrThumbBit) | (thumb ? kCpsrThumbBit : 0u);
        cpu.UpdateCpsrWithFlags(psr);

        cerfengine::ArmCpuState& s = *cpu.State();
        for (uint32_t i = 0; i < kCalleeSavedCount; ++i) {
            s.gprs[kCalleeSavedFirst + i] = callee_saved[i];
        }
        s.gprs[cerfengine::ArmGpr::kR14] = lr;
        s.gprs[cerfengine::ArmGpr::kR13] = frame + 4u * kReturnFrameWords;
        s.gprs[cerfengine::ArmGpr::kR15] = pc & (thumb ? kThumbAddressMask : kArmAddressMask);
        s.gprs[0]                        = kResumedResult;
    }

private:
    uint32_t SkipSaveBlock(uint32_t va) { return va + 4u + 4u * Word(va); }

    uint32_t Word(uint32_t va) {
        const uint8_t* host = emu_.Get<EngineBridge>().Guest().ResolveGuestVaToHost(va);
        if (host == nullptr) {
            emu_.Get<Fatal>().Die("NokiaSeaRayWarmBoot: collapse frame word at VA 0x%08X is unmapped",
                                  va);
        }
        return cerf::le::U32(host);
    }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayWarmBoot, Msm8255WarmBoot);

}
