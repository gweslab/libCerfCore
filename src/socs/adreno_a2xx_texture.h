#pragma once
#include "adreno_a2xx_mmu.h"
#include <cerfcore/core/service.h>
#include "adreno_a2xx_shader.h"
#include <span>

namespace cerfcore {

struct AdrenoA2xxPageCache;

struct AdrenoA2xxSampleRequest {
    AdrenoA2xxVec4        coordinates{};
    const AdrenoA2xxVec4* dx           = nullptr;
    const AdrenoA2xxVec4* dy           = nullptr;
    float                 register_lod = 0;
};

class AdrenoA2xxTexture : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    AdrenoA2xxVec4 Sample(const std::unordered_map<uint32_t,uint32_t>& registers,
        const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels,
        const std::array<uint32_t,6>* descriptor, uint32_t slot,
        const AdrenoA2xxVec4& coordinates, std::array<uint32_t,3> instruction, const AdrenoA2xxVec4* dx,
        const AdrenoA2xxVec4* dy, float register_lod);
    void SampleLanes(const std::unordered_map<uint32_t,uint32_t>& registers,
        const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels,
        const std::array<uint32_t,6>* descriptor, uint32_t slot, std::array<uint32_t,3> instruction,
        std::span<const AdrenoA2xxSampleRequest> requests, std::span<AdrenoA2xxVec4> results);
};

}
