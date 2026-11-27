#pragma once
#include "adreno_a2xx_pixel_job.h"
#include "../cpu/guest_page_fences.h"
#include <cerfcore/core/service.h>
#include <vector>

namespace cerfcore {

class AdrenoA2xxMemory;

class AdrenoA2xxPixelFences : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;
    void Collect(const AdrenoA2xxPrimitiveJob& job, std::vector<GuestPageFences::Range>& writes,
                 std::vector<GuestPageFences::Range>& reads);

private:
    void CollectTarget(const AdrenoA2xxPrimitiveJob& job, const AdrenoA2xxTriangleJob& triangle,
                       std::vector<GuestPageFences::Range>& writes, std::vector<GuestPageFences::Range>& reads);
    void CollectFetches(const AdrenoA2xxPrimitiveJob& job, std::vector<GuestPageFences::Range>& reads);

    AdrenoA2xxMemory* memory_ = nullptr;
};

}
