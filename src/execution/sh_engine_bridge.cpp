#include "engine_bridge.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfengine/sh/sh_cpu.h>
#include "../cpu/sh_processor_config.h"
#include "../tracing/trace_manager.h"

namespace cerfcore {

namespace {

class ShEngineBridge : public EngineBridge {
public:
    using EngineBridge::EngineBridge;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kSh;
    }

    void SaveCpuState(StateWriter&) override {
        emu_.Get<Fatal>().Die("ShEngineBridge: SaveCpuState is not implemented");
    }

    void RestoreCpuState(StateReader&) override {
        emu_.Get<Fatal>().Die("ShEngineBridge: RestoreCpuState is not implemented");
    }

    void SaveMmuState(StateWriter&) override {
        emu_.Get<Fatal>().Die("ShEngineBridge: SaveMmuState is not implemented");
    }

    void RestoreMmuState(StateReader&) override {
        emu_.Get<Fatal>().Die("ShEngineBridge: RestoreMmuState is not implemented");
    }

    void DispatchTraceIter() override {
#if CERF_DEV_MODE
        Trace().DispatchRunLoopIterSh(Engine().Get<cerfengine::ShCpu>().State());
#endif
    }

    void MapInjectionBand(uint32_t va, uint32_t pa, uint32_t size) override {
        emu_.Get<Fatal>().Die("ShEngineBridge: MapInjectionBand va=0x%08X pa=0x%08X size=0x%X is not implemented",
                              va, pa, size);
    }

protected:
    void FillSetup(cerfengine::Setup& setup) override {
        setup.isa    = cerfengine::Isa::kSh;
        setup.sh.dsp = emu_.Get<ShProcessorConfig>().HasDsp();
    }

    void ColdBoot() override {
        cerfengine::ShCpu& cpu = Engine().Get<cerfengine::ShCpu>();
        cpu.ResetState(cerfengine::ShResetKind::kPowerOn);
        LOG(Jit, "ShEngineBridge::ColdBoot: reset PC=0x%08X\n", cpu.State()->pc);
    }
};

}

REGISTER_SERVICE_AS(ShEngineBridge, EngineBridge);

}
