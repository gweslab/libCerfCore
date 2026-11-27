#pragma once
#include "adreno_a2xx_mmu.h"
#include <cerfcore/core/service.h>
#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace cerfcore {

struct AdrenoA2xxPageCache;
struct AdrenoA2xxAluInstruction;
using AdrenoA2xxVec4 = std::array<float, 4>;
inline constexpr unsigned kAdrenoA2xxPositionExport = 62u;
struct AdrenoA2xxMemoryExport { AdrenoA2xxVec4 address, data; };
struct AdrenoA2xxShaderState {
    std::array<AdrenoA2xxVec4, 64> registers{}, exports{};
    std::vector<AdrenoA2xxMemoryExport> memory_exports;
    std::array<AdrenoA2xxVec4, 64> gradients_x{}, gradients_y{};
    uint64_t gradient_mask = 0;
    uint64_t export_mask = 0;
    float texture_lod = 0;
    int32_t loop_address = 0;
    AdrenoA2xxVec4 texture_gradients_x{}, texture_gradients_y{};
    bool killed = false;
    int32_t address_register = 0;
    void ClearForInvocation(uint64_t read_registers, uint64_t written_exports);
};
using AdrenoA2xxTriangle = std::array<const AdrenoA2xxShaderState*, 3>;
inline constexpr unsigned kAdrenoA2xxVaryingSlots = 32u;
struct AdrenoA2xxVaryings {
    uint64_t mask = 0;
    std::array<AdrenoA2xxVec4, kAdrenoA2xxVaryingSlots> values{};
};
class AdrenoA2xxShader : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void Alu(const AdrenoA2xxAluInstruction& instruction, bool pixel,
             const std::unordered_map<uint32_t, uint32_t>& registers,
             AdrenoA2xxShaderState& state, bool& predicate, float& previous);
    void Fetch(const AdrenoA2xxAluInstruction& instruction,
               const std::unordered_map<uint32_t, uint32_t>& registers,
               const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels,
               AdrenoA2xxShaderState& state, bool predicate);
    void FetchLanes(const AdrenoA2xxAluInstruction& instruction,
                    const std::unordered_map<uint32_t, uint32_t>& registers,
                    const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels,
                    std::span<AdrenoA2xxShaderState* const> states, std::span<const bool> predicates);
    uint32_t Register(const std::unordered_map<uint32_t, uint32_t>& registers, uint32_t index);
    [[noreturn]] void Reject(const char* reason, uint32_t value);

private:
    struct PendingSample {
        AdrenoA2xxVec4 coordinates{}, dx{}, dy{};
        bool           gradients   = false;
        float          lod         = 0;
        uint32_t       destination = 0;
        bool           pending     = false;
    };
    void FetchLane(const AdrenoA2xxAluInstruction& instruction,
                   const std::unordered_map<uint32_t, uint32_t>& registers,
                   const AdrenoA2xxMmu& mmu_config, AdrenoA2xxShaderState& state, bool predicate,
                   PendingSample& pending);
    void WriteFetch(const std::array<uint32_t, 3>& words, AdrenoA2xxShaderState& state,
                    uint32_t destination, const AdrenoA2xxVec4& value);
};

}
