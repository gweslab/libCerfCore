#include "../adreno_a2xx_gpu.h"
#include "msm8255_clock_enables.h"
#include "msm8255_clock_reset.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kGpu3dBase = 0xA3500000u;
constexpr uint32_t kGpu3dSize = 0x00020000u;
constexpr uint32_t kCoreClock = 110u;
constexpr uint32_t kGmemClock = 107u;
constexpr int      kGpu3dIrq  = 83;
constexpr uint32_t kGmemBytes = 0x00040000u;

class Msm8255Gpu3d : public AdrenoA2xxGpu {
public:
    explicit Msm8255Gpu3d(CerfEmulator& emu)
        : AdrenoA2xxGpu(emu, kGpu3dBase, kGpu3dSize, kGpu3dIrq, kGmemBytes) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        AdrenoA2xxGpu::OnReady();
        clock_reset_ = &emu_.Get<Msm8255ClockReset>();
        clock_reset_->RegisterListener(kCoreClock, [this] { PowerDown(); });
    }

    uint32_t ReadWord(uint32_t a) override {
        clock_reset_->RequireReleased(kCoreClock, a);
        return AdrenoA2xxGpu::ReadWord(a);
    }

    void WriteWord(uint32_t a, uint32_t v) override {
        clock_reset_->RequireReleased(kCoreClock, a);
        AdrenoA2xxGpu::WriteWord(a, v);
    }

    void RequireCoreRunning(const char* action) override {
        emu_.Get<Msm8255ClockEnables>().RequireRunning(kCoreClock, "gpu3d", action);
    }

    void RequireGmemRunning(const char* action) override {
        emu_.Get<Msm8255ClockEnables>().RequireRunning(kGmemClock, "gpu3d", action);
    }

private:
    Msm8255ClockReset* clock_reset_ = nullptr;
};

}

REGISTER_SERVICE_AS(Msm8255Gpu3d, AdrenoA2xxGpu);

}
