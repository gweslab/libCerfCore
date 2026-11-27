#pragma once
#include "adreno_a2xx_blend.h"
#include "adreno_a2xx_color_layout.h"
#include "adreno_a2xx_depth_stencil.h"
#include "adreno_a2xx_gmem_layout.h"
#include "adreno_a2xx_mmu.h"
#include "adreno_a2xx_resolve_copy.h"
#include "adreno_a2xx_shader.h"
#include "adreno_a2xx_shader_program.h"
#include "adreno_a2xx_triangle_edges.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cerfcore {

struct AdrenoA2xxPixelPoint {
    double x         = 0;
    double y         = 0;
    double inverse_w = 0;
    double z         = 0;
};

struct AdrenoA2xxTriangleJob {
    std::array<AdrenoA2xxVaryings, 3>      varyings{};
    std::array<AdrenoA2xxPixelPoint, 3>    points{};
    std::optional<AdrenoA2xxTriangleEdges> edges;
    std::optional<AdrenoA2xxDepthStencil>  depth_stencil;
    std::optional<AdrenoA2xxBlendEquation> equation;
    std::optional<AdrenoA2xxResolveCopy>   copy;
    std::optional<AdrenoA2xxColorLayout>   source_layout;
    std::optional<AdrenoA2xxColorLayout>   target_layout;
    std::optional<AdrenoA2xxGmemLayout>    color_gmem;
    std::optional<AdrenoA2xxGmemLayout>    depth_gmem;
    std::optional<AdrenoA2xxGmemLayout>    clear_gmem;
    AdrenoA2xxVec4 target_constant{};
    double   original_depth  = 0;
    int      left            = 0;
    int      top             = 0;
    int      right           = 0;
    int      bottom          = 0;
    int      scissor_top     = 0;
    int      scissor_bottom  = 0;
    uint32_t info            = 0;
    uint32_t format          = 0;
    uint32_t swap            = 0;
    uint32_t target_swap     = 0;
    uint32_t pitch           = 0;
    uint32_t bytes           = 0;
    uint64_t base            = 0;
    uint32_t depth_base      = 0;
    uint64_t clear_base      = 0;
    uint32_t color_mask      = 0;
    uint32_t target_mask     = 0;
    uint32_t target_base     = 0;
    uint32_t target_pitch    = 0;
    uint32_t offset_x        = 0;
    uint32_t offset_y        = 0;
    uint8_t* target          = nullptr;
    bool     resolve         = false;
    bool     depth_only      = false;
    bool     blend           = false;
    bool     gmem            = false;
    bool     tiled           = false;
    bool     depth_enabled   = false;
    bool     constant_depth  = false;
    bool     needs_weights   = false;
    bool     front           = false;
    bool     perspective     = false;
    bool     truncate_8888   = false;
    bool     dithered        = false;
    bool     clears_depth    = false;
};

struct AdrenoA2xxPrimitiveJob {
    std::vector<AdrenoA2xxTriangleJob>                             triangles;
    std::shared_ptr<const AdrenoA2xxShaderProgram>                 program;
    std::shared_ptr<const std::unordered_map<uint32_t, uint32_t>> registers;
    AdrenoA2xxMmu                                                  mmu{};
};

}
