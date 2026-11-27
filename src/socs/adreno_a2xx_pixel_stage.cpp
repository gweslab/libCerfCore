#include "adreno_a2xx_pixel_stage.h"
#include "adreno_a2xx_dither.h"
#include "adreno_a2xx_float.h"
#include "adreno_a2xx_fragment_shading.h"
#include "adreno_a2xx_gpu.h"
#include "adreno_a2xx_resolve.h"
#include "adreno_a2xx_socs.h"
#include "adreno_a2xx_tiling.h"
#include "../core/parallel_jobs.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include <algorithm>
#include <functional>

namespace cerfcore {

namespace {

constexpr uint64_t kBandedPixels = 256u;
constexpr uint32_t kBandsPerLane = 4u;

}

REGISTER_SERVICE(AdrenoA2xxPixelStage);

bool AdrenoA2xxPixelStage::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

void AdrenoA2xxPixelStage::OnReady() {
    gmem_.assign(emu_.Get<AdrenoA2xxGpu>().GmemBytes(), 0u);
    memory_   = &emu_.Get<AdrenoA2xxMemory>();
    resolver_ = &emu_.Get<AdrenoA2xxResolve>();
    shading_  = &emu_.Get<AdrenoA2xxFragmentShading>();
    workers_  = &emu_.Get<ParallelJobs>();
    fatal_    = &emu_.Get<Fatal>();
    band_job_ = [this](uint32_t index) { Band(index); };
    workers_->Require();
}

void AdrenoA2xxPixelStage::SaveState(StateWriter& writer) {
    writer.Write("gmem_binding", gmem_binding_);
    writer.WriteBytes("gmem_tiles", gmem_.data(), gmem_.size());
}

void AdrenoA2xxPixelStage::PowerOnReset() {
    std::fill(gmem_.begin(), gmem_.end(), uint8_t{0});
    gmem_binding_ = 0xFFFFFFFFu;
}

void AdrenoA2xxPixelStage::RestoreState(StateReader& reader) {
    reader.Read("gmem_binding", gmem_binding_);
    reader.ReadBytes("gmem_tiles", gmem_.data(), gmem_.size());
    if (gmem_binding_ != 0xFFFFFFFFu &&
        ((gmem_binding_ & 0xFF0u) != 0u || (gmem_binding_ & 0xFFFFF000u) >= gmem_.size() ||
         ((gmem_binding_ & 15u) != 0u && (gmem_binding_ & 15u) != 2u && (gmem_binding_ & 15u) != 5u)))
        reader.Reject("GPU raster GMEM binding 0x%08X", gmem_binding_);
}

void AdrenoA2xxPixelStage::Execute(const AdrenoA2xxPrimitiveJob& job) {
    Begin(job);
    while (Step()) workers_->Run(bands_, band_job_);
}

void AdrenoA2xxPixelStage::Begin(const AdrenoA2xxPrimitiveJob& job) {
    job_ = &job;
    deferred_.size = 0;
    binding_ = gmem_binding_;
    next_triangle_ = 0;
    phase_ = Phase::kSetup;
}

bool AdrenoA2xxPixelStage::Step() {
    if (phase_ == Phase::kBands) MergeTriangle();
    if (SetupTriangle()) return true;
    Finish();
    return false;
}

bool AdrenoA2xxPixelStage::RunNextBand(uint32_t generation) {
    if (phase_ != Phase::kBands || generation != band_generation_) return false;
    const uint32_t index = next_band_.fetch_add(1u, std::memory_order_relaxed);
    if (index >= bands_) return false;
    Band(index);
    return true;
}

void AdrenoA2xxPixelStage::Complete() {
    while (RunNextBand(band_generation_) || Step()) {}
}

bool AdrenoA2xxPixelStage::SetupTriangle() {
    if (next_triangle_ == job_->triangles.size()) return false;
    triangle_ = next_triangle_++;
    const AdrenoA2xxTriangleJob& t = job_->triangles[triangle_];
    if (t.resolve && gmem_binding_ == 0xFFFFFFFFu)
        fatal_->Die("GPU raster unsupported %s value=%08X","uninitialized resolve source",t.info);
    if (t.resolve)
        resolver_->Prepare(t.format,t.swap,t.target_swap,t.truncate_8888,t.bytes,*t.source_layout,*t.target_layout);
    first_row_ = t.top & ~1;
    quad_rows_ = uint32_t(t.bottom - first_row_ + 1) / 2u;
    bands_ = uint64_t(t.right - t.left) * uint64_t(t.bottom - t.top) < kBandedPixels ? 1u :
        (std::min)(quad_rows_, workers_->Lanes() * kBandsPerLane);
    if (band_pixels_.size() < bands_) band_pixels_.resize(bands_);
    if (band_fragments_.size() < bands_) band_fragments_.resize(bands_);
    if (band_texels_.size() < bands_) band_texels_.resize(bands_);
    if (band_targets_.size() < bands_) band_targets_.resize(bands_);
    next_band_.store(0u, std::memory_order_relaxed);
    ++band_generation_;
    phase_ = Phase::kBands;
    return true;
}

void AdrenoA2xxPixelStage::MergeTriangle() {
    const AdrenoA2xxTriangleJob& t = job_->triangles[triangle_];
    uint64_t written = 0;
    for (uint32_t index = 0; index < bands_; ++index) {
        const AdrenoA2xxPixelWrites& band_writes = band_pixels_[index];
        for (size_t k = 0; k < band_writes.size; ++k) deferred_.Push(band_writes.pixels[k]);
        written += band_writes.written + band_writes.size;
    }
    if (t.gmem && !t.resolve && written != 0u) binding_ = t.info & 0xFFFFF00Fu;
}

void AdrenoA2xxPixelStage::Finish() {
    for (size_t k = 0; k < deferred_.size; ++k) {
        const AdrenoA2xxPixelWrite& pixel = deferred_.pixels[k];
        AdrenoA2xxCopyPixelBytes(pixel.target, pixel.data.data(), pixel.bytes);
    }
    gmem_binding_ = binding_;
    phase_ = Phase::kIdle;
}

void AdrenoA2xxPixelStage::Band(uint32_t index) {
    const AdrenoA2xxPrimitiveJob& job = *job_;
    const AdrenoA2xxTriangleJob& t = job.triangles[triangle_];
    auto fail = [&](const char* reason, uint32_t value) {
        fatal_->Die("GPU raster unsupported %s value=%08X",reason,value);
    };
    const AdrenoA2xxDepthStencil& depth_stencil = *t.depth_stencil;
    const AdrenoA2xxGmemLayout&   color_gmem    = *t.color_gmem;
    const AdrenoA2xxGmemLayout&   depth_gmem    = *t.depth_gmem;
    const AdrenoA2xxGmemLayout&   clear_gmem    = *t.clear_gmem;
    const AdrenoA2xxColorLayout&  target_layout = *t.target_layout;
    const AdrenoA2xxTriangleEdges& edges        = *t.edges;
    const auto& points = t.points;
    const bool resolve = t.resolve, depth_only = t.depth_only, gmem = t.gmem, tiled = t.tiled;
    const bool target_swap = t.target_swap == 1u;
    const uint32_t bytes = t.bytes, target_pitch = t.target_pitch, target_base = t.target_base;
    const uint32_t offset_x = t.offset_x, offset_y = t.offset_y, color_mask = t.color_mask;
    const uint32_t target_mask = t.target_mask, depth_base = t.depth_base;
    const int left = t.left, top = t.top, right = t.right, bottom = t.bottom;
    uint8_t* const target = !tiled && gmem && !resolve ? gmem_.data()+t.base : t.target;
    auto channel_byte = [&](float value, unsigned i, int x, int y) {
        return static_cast<uint8_t>(t.truncate_8888 ? value * 255u :
            AdrenoA2xxApproximateDitherQuantize(value,255u,i,uint32_t(x)+offset_x,uint32_t(y)+offset_y,t.dithered));
    };
    auto channel_bits = [&](float value, unsigned i, int x, int y) {
        return AdrenoA2xxApproximateDitherQuantize(value,target_layout.Maximum(i),i,uint32_t(x)+offset_x,
            uint32_t(y)+offset_y,t.dithered);
    };
    auto& resolver = *resolver_;
    auto& memory = *memory_;
    AdrenoA2xxResolve::Rows rows{};
    rows.source = resolve ? gmem_.data()+t.base : nullptr;
    rows.source_layout = &color_gmem;
    rows.clear = resolve && t.clears_depth ? gmem_.data()+t.clear_base : nullptr;
    rows.clear_layout = &clear_gmem;
    rows.defer_clears = t.clears_depth && t.clear_base != t.base &&
        AdrenoA2xxGmemLayout::RowsOverlap(t.base,color_gmem,t.clear_base,clear_gmem,uint32_t(t.scissor_top),
            uint32_t((std::max)(t.scissor_bottom,t.scissor_top + 1)));
    rows.copy = &*t.copy;
    rows.target = target;
    rows.target_base = target_base;
    rows.target_pitch = target_pitch;
    rows.offset_x = offset_x;
    rows.offset_y = offset_y;
    rows.bytes = bytes;
    rows.target_mask = target_mask;
    rows.mmu = &job.mmu;
    auto commit = [&](AdrenoA2xxPixelWrites& out, AdrenoA2xxPageCache& targets, int x, int y,
                      const std::array<double,3>& weights, const AdrenoA2xxShaderState* fragment) {
        const double depth = t.constant_depth ? t.original_depth :
            weights[0]*points[0].z + weights[1]*points[1].z + weights[2]*points[2].z;
        auto depth_passes = [&]() {
            if (!depth_stencil.Attached()) return true;
            uint8_t* const stored = gmem_.data()+depth_base+depth_gmem.Offset(uint32_t(x),uint32_t(y));
            std::array<uint8_t,4> updated{};
            const auto outcome = depth_stencil.Test(depth,t.front,stored,updated);
            if (outcome.writes) {
                AdrenoA2xxCopyPixelBytes(stored,updated.data(),depth_stencil.Bytes());
                ++out.written;
            }
            return outcome.passes;
        };
        AdrenoA2xxVec4 color{};
        if (depth_only) {
            depth_passes();
            return;
        }
        if (!fragment->memory_exports.empty()) fail("pixel memory export",0);
        if (fragment->killed) return;
        if (t.depth_enabled && (fragment->export_mask & ~uint64_t{1}))
            fail("depth fragment exports",static_cast<uint32_t>(fragment->export_mask));
        if (!depth_passes()) return;
        if (color_mask == 0) return;
        if ((fragment->export_mask & 1u) == 0) fail("missing fragment color",0);
        color = fragment->exports[0];
        for (auto& channel : color) {
            if (!AdrenoA2xxFinite(channel)) fail("nonfinite fragment",0);
            channel = std::clamp(channel,0.0f,1.0f);
        }
        const uint64_t address = gmem ? color_gmem.Offset(uint32_t(x),uint32_t(y))
                                                  : (uint64_t(y+offset_y)*target_pitch+x+offset_x)*bytes;
        auto write_color = [&](uint8_t* const destination) {
        std::array<uint8_t,4> old_pixel{};
        AdrenoA2xxCopyPixelBytes(old_pixel.data(),destination,bytes);
        AdrenoA2xxPixelWrite pixel{destination,{},bytes};
        if (target_swap) std::swap(color[0],color[2]);
        const uint32_t mask = target_mask;
        if (t.blend) color = t.equation->Apply(color,target_layout.Unpack(old_pixel.data()),t.target_constant);
        if (bytes == 4u) for (unsigned i = 0; i < 4; ++i)
            pixel.data[i] = (mask & (1u << i)) ? channel_byte(color[i],i,x,y) : old_pixel[i];
        else {
            uint32_t packed = cerf::le::U16(old_pixel.data());
            for (unsigned i = 0; i < target_layout.PackedChannels(); ++i) if (mask & (1u << i))
                packed = (packed & ~(target_layout.Maximum(i) << target_layout.Shift(i))) |
                    (channel_bits(color[i],i,x,y) << target_layout.Shift(i));
            cerf::le::Put16(pixel.data.data(), static_cast<uint16_t>(packed));
        }
        AdrenoA2xxCopyPixelBytes(destination,pixel.data.data(),bytes);
        };
        if (target) write_color(target+address);
        else {
            const uint64_t memory_address = tiled ?
                AdrenoA2xxTiledAddress(target_base,target_pitch,bytes,uint32_t(x),uint32_t(y)) : uint64_t(target_base)+address;
            AdrenoA2xxSpan span;
            write_color(memory.CachedOrSpanWrite(targets,memory_address,bytes,job.mmu,span));
        }
        ++out.written;
    };
    const int first_row = first_row_;
    const uint32_t quad_rows = quad_rows_, bands = bands_;
    auto& out = band_pixels_[index];
    out.size = 0;
    out.written = 0;
    const int row_begin = first_row + int(uint64_t(quad_rows) * index / bands) * 2;
    const int row_end = first_row + int(uint64_t(quad_rows) * (index + 1u) / bands) * 2;
    auto& targets = band_targets_[index];
    targets.Reset();
    if (resolve || depth_only) {
        for (int y = (std::max)(row_begin, top); y < (std::min)(row_end, bottom); ++y) {
            int xl = 0, xr = 0;
            edges.RowSpan(y,left,right,xl,xr);
            if (resolve) {
                resolver.Row(rows,out,targets,y,xl,xr);
                continue;
            }
            for (int x = xl; x < xr; ++x) {
                std::array<double,3> weights{};
                if (t.needs_weights) edges.Covers(x,y,weights);
                commit(out,targets,x,y,weights,nullptr);
            }
        }
        return;
    }
    auto& shading = *shading_;
    const std::array<double,3> inverse_w{points[0].inverse_w,points[1].inverse_w,points[2].inverse_w};
    const AdrenoA2xxShaderProgram& pixel_program = *job.program;
    auto& fragments = band_fragments_[index];
    auto& texels    = band_texels_[index];
    texels.Reset();
    for (int qy = row_begin; qy < row_end; qy += 2) {
        int span_left = right, span_right = left;
        for (int y = qy; y < qy + 2; ++y) {
            if (y < top || y >= bottom) continue;
            int xl = 0, xr = 0;
            edges.RowSpan(y,left,right,xl,xr);
            if (xl >= xr) continue;
            span_left = (std::min)(span_left, xl);
            span_right = (std::max)(span_right, xr);
        }
        if (span_left >= span_right) continue;
        for (int qx = span_left & ~1; qx < span_right; qx += 2) {
            AdrenoA2xxFragmentShading::LaneWeights weights{};
            std::array<bool,4> covered{};
            for (unsigned lane = 0; lane < 4u; ++lane) {
                const int x = qx + int(lane & 1u), y = qy + int(lane >> 1);
                covered[lane] = edges.Covers(x,y,weights[lane]) && x >= left && x < right && y >= top &&
                    y < bottom;
            }
            if (std::none_of(covered.begin(),covered.end(),[](bool lane) { return lane; })) continue;
            for (auto& fragment : fragments)
                fragment.ClearForInvocation(pixel_program.read_registers, pixel_program.written_exports);
            shading.ShadeQuad(t.varyings,weights,inverse_w,t.perspective,covered,pixel_program,*job.registers,
                job.mmu,&texels,fragments);
            for (unsigned lane = 0; lane < 4u; ++lane)
                if (covered[lane])
                    commit(out,targets,qx + int(lane & 1u),qy + int(lane >> 1),weights[lane],&fragments[lane]);
        }
    }
}

}
