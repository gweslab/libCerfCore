#pragma once
#include "adreno_a2xx_mmu.h"
#include "adreno_a2xx_shader_program.h"
#include <cerfcore/core/service.h>
#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace cerfcore {

class AdrenoA2xxShader;

class AdrenoA2xxShaderSequencer : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;
    void Run(const AdrenoA2xxShaderProgram& program, const std::unordered_map<uint32_t, uint32_t>& registers,
             const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels, AdrenoA2xxShaderState& state);
    void RunQuad(const AdrenoA2xxShaderProgram& program, const std::unordered_map<uint32_t, uint32_t>& registers,
                 const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels,
                 std::array<AdrenoA2xxShaderState, 4>& states);

private:
    void RunInvocations(const AdrenoA2xxShaderProgram& program,
                        const std::unordered_map<uint32_t, uint32_t>& registers, const AdrenoA2xxMmu& mmu_config,
                        AdrenoA2xxPageCache* texels, std::span<AdrenoA2xxShaderState> states);

    AdrenoA2xxShader* shader_ = nullptr;
};

}
