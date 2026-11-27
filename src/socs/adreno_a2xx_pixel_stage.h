#pragma once
#include "adreno_a2xx_memory.h"
#include "adreno_a2xx_pixel_job.h"
#include "adreno_a2xx_pixel_writes.h"
#include <cerfcore/core/service.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class AdrenoA2xxFragmentShading;
class AdrenoA2xxResolve;
class Fatal;
class ParallelJobs;

class AdrenoA2xxPixelStage : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;
    void SaveState(class StateWriter& writer);
    void RestoreState(class StateReader& reader);
    void PowerOnReset();
    void Execute(const AdrenoA2xxPrimitiveJob& job);

    void     Begin(const AdrenoA2xxPrimitiveJob& job);
    bool     Step();
    bool     RunNextBand(uint32_t generation);
    void     Complete();
    uint32_t Bands() const { return bands_; }
    uint32_t BandGeneration() const { return band_generation_; }

private:
    enum class Phase : uint8_t { kIdle, kSetup, kBands };

    bool SetupTriangle();
    void Band(uint32_t index);
    void MergeTriangle();
    void Finish();

    AdrenoA2xxMemory*                                memory_   = nullptr;
    AdrenoA2xxResolve*                               resolver_ = nullptr;
    AdrenoA2xxFragmentShading*                       shading_  = nullptr;
    ParallelJobs*                                    workers_  = nullptr;
    Fatal*                                           fatal_    = nullptr;
    std::function<void(uint32_t)>                    band_job_;
    std::vector<uint8_t>                             gmem_;
    uint32_t                                         gmem_binding_ = 0xFFFFFFFFu;
    const AdrenoA2xxPrimitiveJob*                    job_ = nullptr;
    Phase                                            phase_ = Phase::kIdle;
    size_t                                           next_triangle_ = 0;
    size_t                                           triangle_ = 0;
    uint32_t                                         binding_ = 0;
    uint32_t                                         bands_ = 0;
    uint32_t                                         band_generation_ = 0;
    int                                              first_row_ = 0;
    uint32_t                                         quad_rows_ = 0;
    std::atomic<uint32_t>                            next_band_{0};
    AdrenoA2xxPixelWrites                            deferred_;
    std::vector<AdrenoA2xxPixelWrites>               band_pixels_;
    std::vector<std::array<AdrenoA2xxShaderState,4>> band_fragments_;
    std::vector<AdrenoA2xxPageCache>                 band_texels_;
    std::vector<AdrenoA2xxPageCache>                 band_targets_;
};

}
