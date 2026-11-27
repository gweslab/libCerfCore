#pragma once
#include "adreno_a2xx_mmu.h"
#include "adreno_a2xx_pixel_job.h"
#include <cerfcore/core/service.h>
#include "adreno_a2xx_shader_program.h"
#include <memory>
#include <span>
#include <unordered_map>

namespace cerfcore {

class AdrenoA2xxRaster : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;
    void Primitive(std::span<const AdrenoA2xxTriangle> triangles,
        const std::unordered_map<uint32_t,uint32_t>& registers,
        std::shared_ptr<const AdrenoA2xxShaderProgram> pixel_program,
        std::shared_ptr<const std::unordered_map<uint32_t,uint32_t>> pixel_registers,
        const AdrenoA2xxMmu& mmu_config);
private:
    void SetupTriangle(const AdrenoA2xxTriangle& vertices, const AdrenoA2xxTriangle& depth_vertices,
        const std::unordered_map<uint32_t,uint32_t>& registers,
        const AdrenoA2xxShaderProgram& pixel_program, AdrenoA2xxPrimitiveJob& job);
    uint64_t gmem_bytes_ = 0;
};

}
