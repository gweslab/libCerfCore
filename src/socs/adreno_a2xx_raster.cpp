#include "adreno_a2xx_raster.h"
#include "adreno_a2xx_clipper.h"
#include "adreno_a2xx_gpu.h"
#include "adreno_a2xx_memory.h"
#include "adreno_a2xx_pixel_queue.h"
#include "adreno_a2xx_regs.h"
#include "adreno_a2xx_scissor.h"
#include "adreno_a2xx_viewport.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "adreno_a2xx_socs.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>

namespace cerfcore {

namespace {

using adreno_a2xx_regs::kClipDisable;
using adreno_a2xx_regs::kClipDirectX;
constexpr uint32_t kClipModeled = kClipDisable | kClipDirectX | adreno_a2xx_regs::kClipVertexKillOr;

AdrenoA2xxVaryings Varyings(const AdrenoA2xxShaderState& vertex) {
    AdrenoA2xxVaryings out;
    out.mask = vertex.export_mask & 0xFFFFFFFFu;
    for (uint64_t rest = out.mask; rest != 0u; rest &= rest - 1u) {
        const unsigned slot = static_cast<unsigned>(std::countr_zero(rest));
        out.values[slot] = vertex.exports[slot];
    }
    return out;
}

}

REGISTER_SERVICE(AdrenoA2xxRaster);
bool AdrenoA2xxRaster::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}
void AdrenoA2xxRaster::OnReady() {
    gmem_bytes_ = emu_.Get<AdrenoA2xxGpu>().GmemBytes();
}

void AdrenoA2xxRaster::Primitive(std::span<const AdrenoA2xxTriangle> triangles,
    const std::unordered_map<uint32_t,uint32_t>& registers,
    std::shared_ptr<const AdrenoA2xxShaderProgram> pixel_program,
    std::shared_ptr<const std::unordered_map<uint32_t,uint32_t>> pixel_registers,
    const AdrenoA2xxMmu& mmu_config) {
    AdrenoA2xxPrimitiveJob job;
    job.program = std::move(pixel_program);
    job.registers = std::move(pixel_registers);
    job.mmu = mmu_config;
    job.mmu.pixel_stage = true;
    const auto clip = registers.find(adreno_a2xx_regs::kIdxPaClClipCntl);
    const auto vte = registers.find(0x2206);
    const auto raster = registers.find(0x2205);
    auto fail = [&](const char* reason, uint32_t value) {
        emu_.Get<Fatal>().Die("GPU raster unsupported %s value=%08X",reason,value);
    };
    if (clip != registers.end() && (clip->second & ~kClipModeled) != 0u) fail("clip controls",clip->second);
    const AdrenoA2xxViewportTransform transform{vte != registers.end() ? vte->second : 0u};
    const bool directx_z = clip != registers.end() && (clip->second & kClipDirectX) != 0u;
    const bool enabled = clip != registers.end() && (clip->second & kClipDisable) == 0u && vte != registers.end() &&
        transform.Valid() && !transform.WindowSpace() &&
        raster != registers.end() && (raster->second & 0xC0000000u) != 0x40000000u;
    const AdrenoA2xxClipper clipper{directx_z, raster != registers.end() && (raster->second & 0x100000u) != 0u};
    for (const auto& vertices : triangles) {
        bool outside = false;
        for (unsigned i = 0; enabled && i < 3u; ++i) {
            const auto& p = vertices[i]->exports[kAdrenoA2xxPositionExport];
            if (!(vertices[i]->export_mask & (uint64_t{1} << kAdrenoA2xxPositionExport))) fail("missing position",i);
            for (float component : p) if (!std::isfinite(component)) fail("homogeneous clipping",i);
            if (p[3] <= 0.0f) fail("homogeneous clipping",i);
            if (!transform.TakesReciprocalW() && p[3] != 1.0f) fail("premultiplied nonunit W",i);
            outside |= clipper.Outside(p);
        }
        if (!outside) {
            SetupTriangle(vertices,vertices,registers,*job.program,job);
            continue;
        }
        if (raster->second & 0xC000B818u) fail("MSAA/polygon/faceness",raster->second);
        const std::vector<AdrenoA2xxShaderState> polygon = clipper.Clip({*vertices[0],*vertices[1],*vertices[2]});
        for (size_t i = 1; i+1 < polygon.size(); ++i)
            SetupTriangle({&polygon[0],&polygon[i],&polygon[i+1]},vertices,registers,*job.program,job);
    }
    if (!job.triangles.empty()) emu_.Get<AdrenoA2xxPixelQueue>().Submit(std::move(job));
}

/* Mesa e97ad748 a2xx.xml: PA_CL_VTE_CNTL, PA_SU_VTX_CNTL, RB_COLOR_INFO;
   fd2_gmem.c: fmt2swap, fd2_emit_sysmem_prep, fd2_emit_tile_renderprep. */
void AdrenoA2xxRaster::SetupTriangle(const AdrenoA2xxTriangle& vertices, const AdrenoA2xxTriangle& depth_vertices,
    const std::unordered_map<uint32_t,uint32_t>& registers,
    const AdrenoA2xxShaderProgram& pixel_program, AdrenoA2xxPrimitiveJob& job) {
    auto fail = [&](const char* reason, uint32_t value) {
        emu_.Get<Fatal>().Die("GPU raster unsupported %s value=%08X",reason,value);
    };
    for (const auto* vertex : vertices)
        if ((vertex->export_mask & (uint64_t{1} << kAdrenoA2xxPositionExport)) == 0) fail("missing position",0);
    const auto& p0 = vertices[0]->exports[kAdrenoA2xxPositionExport];
    const auto& p1 = vertices[1]->exports[kAdrenoA2xxPositionExport];
    const auto& p2 = vertices[2]->exports[kAdrenoA2xxPositionExport];
    if (p0 == p1 || p1 == p2 || p2 == p0) return;
    auto reg = [&](uint32_t index) {
        const auto i = registers.find(index);
        if (i == registers.end()) fail("missing register",index);
        return i->second;
    };
    const uint32_t raster = reg(0x2205);
    if ((raster & 0xC0000000u) == 0x40000000u) return;
    const AdrenoA2xxViewportTransform transform{reg(0x2206)};
    const uint32_t control = reg(0x2202);
    const uint32_t clip = reg(adreno_a2xx_regs::kIdxPaClClipCntl);
    if ((clip & ~kClipModeled) != 0u) fail("clip controls",clip);
    if (!transform.Valid()) fail("viewport format",transform.Value());
    if ((raster & 0xC000B818u) != 0) fail("MSAA/polygon/faceness",raster);
    const AdrenoA2xxDepthStencil depth_stencil = AdrenoA2xxDepthStencil::Read(reg);
    const bool depth_enabled = depth_stencil.DepthTest();
    const uint32_t mode = reg(0x2208);
    if (mode != 4u && mode != 5u && mode != 6u) fail("render mode",mode);
    const bool resolve = mode == 6u;
    const bool depth_only = mode == 5u;
    const uint32_t dither_mode = (control >> 12) & 3u;
    const bool blend = !resolve && (control & 0x20u) == 0;
    if (!resolve) {
        const uint32_t color_control = control & ~0x3007u;
        if (blend ? color_control != 0xC00u : color_control != 0x20u && color_control != 0xC20u)
            fail("blend/alpha/ROP/dither type",control);
        if (dither_mode == 3u) fail("dither mode",control);
    }
    const uint32_t blend_control = blend ? reg(0x2201u) : 0u;
    const AdrenoA2xxBlendEquation equation{blend_control};
    AdrenoA2xxVec4 blend_constant{};
    if (blend) {
        if (const char* unsupported = equation.Unsupported()) fail(unsupported,blend_control);
        if (equation.UsesConstant()) for (unsigned i = 0; i < 4u; ++i) {
            const uint32_t component = reg(0x2105u + i);
            if (component > 255u) fail("blend constant",component);
            blend_constant[i] = static_cast<float>(component) / 255.0f;
        }
    }
    if (resolve && depth_stencil.Attached()) fail("resolve depth/stencil",depth_stencil.Control());
    if (transform.WindowSpace() && !resolve && (clip & kClipDisable) == 0u) fail("window-space clipping",clip);
    const uint32_t info = reg(0x2001);
    const uint32_t surface = reg(0x2000);
    const uint32_t format = info & 15u;
    if ((surface & ~0x3FFFu) != 0 || (surface & 0x3FFFu) == 0) fail("surface/MSAA",surface);
    if (format != 0u && format != 2u && format != 5u) fail("color format",format);
    const uint32_t swap = (info >> 9) & 3u;
    if ((info & 0x180u) != 0 || (swap > 1u && !(format == 0u && swap == 3u))) fail("endian/swap",info);
    const AdrenoA2xxColorLayout source_layout{format, swap};
    const bool nonlinear = (info & 0x40u) == 0;
    if (nonlinear) {
        const uint32_t edram = reg(0xF02);
        if ((edram & 0xFFFFC030u) != 0u || (uint64_t{0x4000u} << (edram & 15u)) != gmem_bytes_)
            fail("GMEM configuration",edram);
    }
    const bool gmem = nonlinear && (info & 0xFFFFF000u) < gmem_bytes_;
    const bool tiled = nonlinear && !gmem;
    if (tiled && (surface & 31u)) fail("tiled target pitch",surface);
    if (gmem) {
        emu_.Get<AdrenoA2xxGpu>().RequireGmemRunning("accesses its graphics memory");
        if (swap == 3u) fail("GMEM swap",info);
        if ((surface & (kAdrenoA2xxGmemTileSide - 1u)) != 0u) fail("GMEM pitch",surface);
    }
    if (resolve && !gmem) fail("uninitialized resolve source",info);
    const uint32_t pitch = surface & 0x3FFFu;
    const uint32_t bytes = source_layout.Bytes();
    const uint32_t depth_base = depth_stencil.Base();
    if (const char* unsupported = depth_stencil.Unsupported()) fail(unsupported,depth_stencil.Info());
    if (depth_stencil.Attached() && (!gmem || depth_base >= gmem_bytes_)) fail("depth attachment",depth_stencil.Info());
    uint32_t color_mask   = depth_only ? 0u : reg(0x2104);
    uint32_t target_info  = info;
    uint32_t target_pitch = pitch;
    uint32_t offset_x     = 0;
    uint32_t offset_y     = 0;
    uint32_t target_base = info & 0xFFFFF000u;
    const auto draw_copy_control = registers.find(0x2318);
    if (!resolve && draw_copy_control != registers.end() &&
        AdrenoA2xxResolveCopy::ControlClears(draw_copy_control->second))
        fail("copy-control clear on a draw",draw_copy_control->second);
    const uint32_t copy_control = resolve ? reg(0x2318) : 0u;
    const bool clears_resolved_depth = AdrenoA2xxResolveCopy::ControlClearsDepth(copy_control);
    const AdrenoA2xxResolveCopy copy{copy_control, resolve ? reg(0x2319) : 0u, resolve ? reg(0x231A) : 0u,
        resolve ? reg(0x231B) : 0u, resolve ? reg(0x231C) : 0u, info,
        clears_resolved_depth ? reg(0x2002) : 0u, clears_resolved_depth ? reg(0x231D) : 0u};
    if (resolve) {
        if (const char* unsupported = copy.Unsupported()) fail(unsupported,copy.UnsupportedValue());
        target_base = copy.DestBase();
        target_pitch = copy.DestPitch();
        color_mask = copy.ColorMask();
        target_info = copy.DestColorInfo();
        offset_x = copy.OffsetX();
        offset_y = copy.OffsetY();
    }
    if ((color_mask & ~15u) != 0) fail("color mask",color_mask);
    const uint32_t vtx = reg(0x2302);
    if (vtx != 5u) fail("pixel center/quantization",vtx);
    std::array<AdrenoA2xxPixelPoint,3> points{};
    auto window_depth = [&](const AdrenoA2xxVec4& p) {
        double z = p[2] * (transform.ZPremultiplied() ? 1.0 : transform.InverseW(p[3]));
        if (transform.Scale(2)) z *= std::bit_cast<float>(reg(0x2113));
        if (transform.Offset(2)) z += std::bit_cast<float>(reg(0x2114));
        return z;
    };
    const double original_depth = depth_enabled ? window_depth(depth_vertices[0]->exports[kAdrenoA2xxPositionExport]) : 0.0;
    const bool constant_depth = !depth_enabled ||
        (original_depth == window_depth(depth_vertices[1]->exports[kAdrenoA2xxPositionExport]) &&
         original_depth == window_depth(depth_vertices[2]->exports[kAdrenoA2xxPositionExport]));
    for (unsigned i = 0; i < 3; ++i) {
        const auto& p = vertices[i]->exports[kAdrenoA2xxPositionExport];
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) ||
            !std::isfinite(p[3]) || (!transform.WindowSpace() && p[3] <= 0.0f)) fail("homogeneous clipping",i);
        if (!transform.TakesReciprocalW() && p[3] != 1.0f && !transform.WindowSpace())
            fail("premultiplied nonunit W",i);
        const double inverse_w = transform.InverseW(p[3]);
        const double xy_weight = transform.XyPremultiplied() ? 1.0 : inverse_w;
        double x = p[0] * xy_weight;
        double y = p[1] * xy_weight;
        if (transform.Scale(0)) x *= std::bit_cast<float>(reg(0x210F));
        if (transform.Offset(0)) x += std::bit_cast<float>(reg(0x2110));
        if (transform.Scale(1)) y *= std::bit_cast<float>(reg(0x2111));
        if (transform.Offset(1)) y += std::bit_cast<float>(reg(0x2112));
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 32768 || std::abs(y) > 32768)
            fail("viewport coordinate range",i);
        double z = 0;
        if (depth_enabled) {
            z = constant_depth ? original_depth : window_depth(p);
            if (!std::isfinite(z) || z < 0.0 || z > 1.0)
                fail("out-of-range depth",std::bit_cast<uint32_t>(static_cast<float>(z)));

        }
        points[i] = {std::nearbyint(x * 16.0) / 16.0,std::nearbyint(y * 16.0) / 16.0,inverse_w,z};
    }
    double area = AdrenoA2xxTriangleEdges::Edge({points[0].x,points[0].y},{points[1].x,points[1].y},
                                                points[2].x,points[2].y);
    if (area == 0.0) return;
    const bool front = (area > 0.0) == ((raster & 4u) != 0);
    if (raster & (front ? 1u : 2u)) return;
    const double sign = area < 0.0 ? -1.0 : 1.0;
    area *= sign;
    const AdrenoA2xxScissor scissor{reg(0x2080), reg(0x2081), reg(0x2082), reg(0x200E), reg(0x200F),
        (raster & 0x10000u) != 0u};
    for (auto& p : points) {
        p.x += scissor.OffsetX();
        p.y += scissor.OffsetY();
    }
    const int left = (std::max)(scissor.Left(),int(std::floor((std::min)({points[0].x,points[1].x,points[2].x}))));
    const int top = (std::max)(scissor.Top(),int(std::floor((std::min)({points[0].y,points[1].y,points[2].y}))));
    const int right = (std::min)(scissor.Right(),int(std::ceil((std::max)({points[0].x,points[1].x,points[2].x}))));
    const int bottom = (std::min)(scissor.Bottom(),int(std::ceil((std::max)({points[0].y,points[1].y,points[2].y}))));
    if (left >= right || top >= bottom || (color_mask == 0 && !depth_stencil.MayWrite() && !clears_resolved_depth)) return;
    if (left < 0 || top < 0 || right > static_cast<int>(pitch)) fail("target bounds",pitch);
    if (!resolve && !depth_only && pixel_program.words.empty()) fail("missing pixel shader",0);
    const uint64_t base = info & 0xFFFFF000u;
    const AdrenoA2xxGmemLayout color_gmem{pitch, bytes};
    const AdrenoA2xxGmemLayout depth_gmem{pitch, depth_stencil.Bytes()};
    if (gmem && base+color_gmem.RowsEnd(uint32_t(bottom)) > gmem_bytes_) fail("GMEM capacity",static_cast<uint32_t>(base));
    if (depth_stencil.Attached() && depth_base+depth_gmem.RowsEnd(uint32_t(bottom)) > gmem_bytes_)
        fail("depth GMEM capacity",depth_base);
    if (depth_stencil.Attached() && color_mask &&
        AdrenoA2xxGmemLayout::RowsOverlap(base,color_gmem,depth_base,depth_gmem,uint32_t(top),uint32_t(bottom)))
        fail("overlapping depth/color attachments",depth_base);
    const uint64_t clear_base = copy.DepthBase();
    const AdrenoA2xxGmemLayout clear_gmem{pitch, copy.DepthBytes()};
    if (clears_resolved_depth && clear_base+clear_gmem.RowsEnd(uint32_t(bottom)) > gmem_bytes_)
        fail("depth clear GMEM capacity",copy.DepthBase());
    if (clears_resolved_depth && (clear_base > base || copy.DepthBytes() != bytes) &&
        AdrenoA2xxGmemLayout::RowsOverlap(base,color_gmem,clear_base,clear_gmem,uint32_t(top),uint32_t(bottom)))
        fail("depth clear overlapping its resolve source",copy.DepthBase());
    AdrenoA2xxTriangleJob& t = job.triangles.emplace_back();
    if (!tiled && !(gmem && !resolve)) t.target = emu_.Get<AdrenoA2xxMemory>().UntranslatedWrite(target_base,
        (uint64_t(bottom-1+offset_y)*target_pitch+right+offset_x)*bytes,job.mmu);
    const bool needs_weights = !(resolve || depth_only) || !constant_depth;
    for (unsigned i = 0; i < 3u; ++i) t.varyings[i] = Varyings(*vertices[i]);
    t.points = points;
    t.edges.emplace(std::array<AdrenoA2xxTriangleEdges::Vertex,3>{{{points[0].x,points[0].y},
        {points[1].x,points[1].y},{points[2].x,points[2].y}}},sign,area,needs_weights);
    t.depth_stencil.emplace(depth_stencil);
    t.equation.emplace(equation);
    t.copy.emplace(copy);
    t.source_layout.emplace(source_layout);
    t.target_layout.emplace(format, (target_info >> 9) & 3u);
    t.color_gmem.emplace(color_gmem);
    t.depth_gmem.emplace(depth_gmem);
    t.clear_gmem.emplace(clear_gmem);
    t.target_swap = (target_info >> 9) & 3u;
    t.target_constant = blend_constant;
    if (t.target_swap == 1u) std::swap(t.target_constant[0],t.target_constant[2]);
    t.target_mask = t.target_swap == 1u
        ? (color_mask & 10u) | ((color_mask & 1u) << 2) | ((color_mask & 4u) >> 2) : color_mask;
    t.original_depth = original_depth;
    t.left = left;
    t.top = top;
    t.right = right;
    t.bottom = bottom;
    t.scissor_top = (std::max)(scissor.Top(),0);
    t.scissor_bottom = scissor.Bottom();
    t.info = info;
    t.format = format;
    t.swap = swap;
    t.pitch = pitch;
    t.bytes = bytes;
    t.base = base;
    t.depth_base = depth_base;
    t.clear_base = clear_base;
    t.color_mask = color_mask;
    t.target_base = target_base;
    t.target_pitch = target_pitch;
    t.offset_x = offset_x;
    t.offset_y = offset_y;
    t.resolve = resolve;
    t.depth_only = depth_only;
    t.blend = blend;
    t.gmem = gmem;
    t.tiled = tiled;
    t.depth_enabled = depth_enabled;
    t.constant_depth = constant_depth;
    t.needs_weights = needs_weights;
    t.front = front;
    t.perspective = (raster & 0x100000u) == 0;
    t.truncate_8888 = (target_info & 0x30u) == 0u && (resolve || dither_mode == 0u);
    t.dithered = !resolve && dither_mode != 0u;
    t.clears_depth = clears_resolved_depth;
}

}
