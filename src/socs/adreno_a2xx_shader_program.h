#pragma once
#include "adreno_a2xx_shader.h"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cerfcore {

struct AdrenoA2xxAluSource {
    bool           temporary   = false;
    bool           relative    = false;
    bool           absolute    = false;
    bool           use_address = false;
    bool           export32    = false;
    bool           negate      = false;
    bool           resolved    = false;
    uint8_t        index       = 0;
    uint8_t        swizzle     = 0;
    AdrenoA2xxVec4 value{};
};

struct AdrenoA2xxAluInstruction {
    std::array<uint32_t, 3>            words{};
    std::array<AdrenoA2xxAluSource, 3> sources{};
    AdrenoA2xxAluSource                scalar_source{};
    std::array<uint32_t, 6>            texture_state{};
    bool                               texture_resolved = false;
};

struct AdrenoA2xxShaderProgram {
    std::span<const uint32_t>             words;
    bool                                  pixel        = false;
    const char*                           reject       = nullptr;
    uint32_t                              reject_value = 0;
    uint32_t                              limit        = 0;
    std::vector<uint64_t>                 control;
    std::vector<AdrenoA2xxAluInstruction> alu;
    std::vector<uint16_t>                 fetches;
    uint64_t                              read_registers  = 0;
    uint64_t                              written_exports = 0;
    bool                                  straight        = false;
    std::vector<uint16_t>                 straight_steps;
    bool                                  unresolved_constants = false;
    bool                                  unresolved_fetches   = false;
};

inline constexpr uint16_t kAdrenoA2xxStraightFetch = 0x8000u;

}
