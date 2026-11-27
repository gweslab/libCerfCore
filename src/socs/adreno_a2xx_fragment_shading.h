#pragma once

#include "adreno_a2xx_mmu.h"
#include "adreno_a2xx_shader_program.h"

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace cerfcore {

class AdrenoA2xxFragmentShading : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    using LaneWeights = std::array<std::array<double, 3>, 4>;

    void ShadeQuad(const std::array<AdrenoA2xxVaryings, 3>& vertices, const LaneWeights& weights,
                   const std::array<double, 3>& inverse_w, bool perspective,
                   const std::array<bool, 4>& covered, const AdrenoA2xxShaderProgram& pixel_program,
                   const std::unordered_map<uint32_t, uint32_t>& registers,
                   const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels,
                   std::array<AdrenoA2xxShaderState, 4>& fragments);
};

}
