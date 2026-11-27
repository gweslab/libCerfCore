#include "../adreno_a2xx_gpu.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "imx51_id.h"
#include "imx51_power_gate_line.h"
#include "../freescale_module_clocks.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kGpu3dBase = 0x30000000u;
constexpr uint32_t kGpu3dSize = 0x00030000u;
constexpr int      kGpu3dIrq  = 12;
constexpr uint32_t kGmemBytes = 0x00020000u;

class Imx51Gpu3d : public AdrenoA2xxGpu {
public:
    explicit Imx51Gpu3d(CerfEmulator& emu)
        : AdrenoA2xxGpu(emu, kGpu3dBase, kGpu3dSize, kGpu3dIrq, kGmemBytes) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx51;
    }
    void OnReady() override {
        AdrenoA2xxGpu::OnReady();
        emu_.Get<Imx51PowerGateLine>().RegisterBlock(Imx51PowerGatedBlock::kGpu3d,
                                                     [this] { PowerDown(); });
    }

    void RequireCoreRunning(const char* action) override {
        emu_.Get<FreescaleModuleClocks>().RequireRunning(FreescaleModule::kGpu3dCore, action);
    }

    void RequireGmemRunning(const char* action) override {
        emu_.Get<FreescaleModuleClocks>().RequireRunning(FreescaleModule::kGpu3dMemory, action);
    }
};

}

REGISTER_SERVICE_AS(Imx51Gpu3d, AdrenoA2xxGpu);

}
